// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_simulation_unit.h"
#include "ivp_environment.h"
#include "ivp_controller.h"
#include "ivp_real_object.h"
#include "../ivp_utility/ivp_random.h"
#include "../ivp_utility/ivp_u_memory.h"

#include <cstdlib>
#include <cmath>
#include <cstring>
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte

// ============================================================================
// FUN_06348e10: IVP_Simulation_Unit's constructor (see the header).
// ============================================================================
IVP_Simulation_Unit::IVP_Simulation_Unit()
    : State(8)
    , Prev(nullptr) // port deviation: retail leaves Prev/Next uninitialized
    , Next(nullptr)
{
    // Cores: inline buffer at +0x14, memsize 2. ControllerBuckets: {0, 0, null}.
}

// ============================================================================
// FUN_06348ef0: backward search of Unit->Cores, then shift-remove (the
// IVP_U_Vector<T>::RemoveElement algorithm).
// ============================================================================
static void IVP_Simulation_Unit_Remove_Core(IVP_Simulation_Unit* Unit, IVP_Core* Core)
{
    Unit->Cores.RemoveElement(Core);
}

// ============================================================================
// FUN_06348c80: backward search of ControllerBuckets for the bucket whose
// Controller matches; -1 when absent.
// ============================================================================
static int IVP_Simulation_Unit_Find_Controller_Bucket_Index(IVP_Simulation_Unit* Unit, IVP_Controller* Controller)
{
    for (int i = static_cast<int>(Unit->ControllerBuckets.n_elems) - 1; i >= 0; --i)
    {
        if (Unit->ControllerBuckets.ElementAt(i)->Controller == Controller)
        {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// FUN_06348e60: re-resolves the bucket index (retail searches again rather
// than reusing the caller's), deletes the bucket and removes its slot.
// ============================================================================
static void IVP_Simulation_Unit_Free_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller)
{
    // Reproduced on purpose: no not-found test in retail; an absent controller
    // (index -1) reads elems[-1] and shifts from -1.
    const int32_t Index = IVP_Simulation_Unit_Find_Controller_Bucket_Index(Unit, Controller);

    IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[Index];
    delete Bucket; // FUN_06348be0 (the Cores vector's Clear) + operator delete

    // Shift down while i < n-1 (n re-read), then n--.
    for (int32_t I = Index; I < static_cast<int32_t>(Unit->ControllerBuckets.n_elems) - 1; ++I)
    {
        Unit->ControllerBuckets.elems[I] = Unit->ControllerBuckets.elems[I + 1];
    }
    --Unit->ControllerBuckets.n_elems;
}

// ============================================================================
// FUN_06349430: intrusive doubly-linked-list unlink. HeadSlot is whichever
// of the manager's two list heads (+0x14 / +0x1a4) owns Node.
// ============================================================================
static void IVP_Simulation_Unit_List_Unlink(IVP_Simulation_Unit* Node, IVP_Simulation_Unit** HeadSlot)
{
    IVP_Simulation_Unit* Prev = Node->Prev;
    IVP_Simulation_Unit* Next = Node->Next;

    if (Prev != nullptr)
    {
        Prev->Next = Next;
    }
    else
    {
        *HeadSlot = Next;
    }

    if (Next != nullptr)
    {
        Next->Prev = Prev;
    }
}

// ============================================================================
// FUN_06349380: IVP_Sim_Units_Manager's constructor, stores in retail's order.
// +0x18..+0x1a3 stay as allocated.
// ============================================================================
IVP_Sim_Units_Manager::IVP_Sim_Units_Manager(IVP_Environment* Environment)
{
    environment = Environment;
    sim_units_head = nullptr;
    still_units_head = nullptr;
    Field_0x04 = 9.0f;
    Field_0x08 = 0.729999542f; // 0x3f3ae140 exactly
    Field_0x0c = 0.0f;
    Field_0x10 = 0.3f; // 0x3e99999a
}

// ============================================================================
// FUN_06349460: rem_sim_unit_from_manager. State's signed low byte < 8 picks
// the active head (+0x14), else the still head (+0x1a4). Callers 06348c4b,
// 06348d57, 063494cc, 06349667.
// ============================================================================
void IVP_Sim_Units_Manager::rem_sim_unit_from_manager(IVP_Simulation_Unit* Unit)
{
    IVP_Simulation_Unit** HeadSlot = (static_cast<int8_t>(Unit->State & 0xFF) < 8)
        ? &sim_units_head
        : &still_units_head;

    IVP_Simulation_Unit_List_Unlink(Unit, HeadSlot);
}

// ============================================================================
// FUN_063493e0: push-front, the inverse of FUN_06349430.
// ============================================================================
static void IVP_Simulation_Unit_List_Push_Front(IVP_Simulation_Unit* Node, IVP_Simulation_Unit** HeadSlot)
{
    IVP_Simulation_Unit* OldHead = *HeadSlot;

    Node->Next = OldHead;
    if (OldHead != nullptr)
    {
        OldHead->Prev = Node;
    }
    Node->Prev = nullptr;
    *HeadSlot = Node;
}

// ============================================================================
// FUN_06349400: add_sim_unit_to_manager, same head selection. Callers
// 063476d0, 063489cc, 063494d8, 0634967c.
// ============================================================================
void IVP_Sim_Units_Manager::add_sim_unit_to_manager(IVP_Simulation_Unit* Unit)
{
    IVP_Simulation_Unit** HeadSlot = (static_cast<int8_t>(Unit->State & 0xFF) < 8)
        ? &sim_units_head
        : &still_units_head;

    IVP_Simulation_Unit_List_Push_Front(Unit, HeadSlot);
}

// ============================================================================
// FUN_06348d80: the unit's destructor body (~IVP_Simulation_Unit; callers free
// the unit afterwards). Clears the controller buckets (FUN_06348b80), then
// releases ControllerBuckets' buffer before Cores' (heap buffers only). The SEH
// frame is not reproduced.
// ============================================================================
void IVP_Simulation_Unit_Free_Vector_Buffers(IVP_Simulation_Unit* Unit)
{
    IVP_Simulation_Unit_Clear_Controller_Buckets(Unit); // FUN_06348b80
    Unit->ControllerBuckets.Clear();
    Unit->Cores.Clear();
}

// ============================================================================
// FUN_06348cb0: detach_core (see the header). Called from IVP_Core::~IVP_Core
// (FUN_063477e0) via IVP_Core_Remove_Object.
// ============================================================================
void IVP_Simulation_Unit_Detach_Core(IVP_Simulation_Unit* Unit, IVP_Core* Core)
{
    IVP_Simulation_Unit_Remove_Core(Unit, Core);

    // Remove Core from every controller's bucket; an emptied bucket is freed.
    for (int i = static_cast<int>(Core->Controllers.n_elems) - 1; i >= 0; --i)
    {
        IVP_Controller* Controller = Core->Controllers.ElementAt(i);

        // No not-found test: retail indexes elems[-1].
        const int32_t BucketIndex = IVP_Simulation_Unit_Find_Controller_Bucket_Index(Unit, Controller);
        IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[BucketIndex];
        Bucket->Cores.RemoveElement(Core);

        if (Bucket->Cores.n_elems == 0)
        {
            IVP_Simulation_Unit_Free_Controller_Bucket(Unit, Controller);
        }
    }

    // The last core gone: unlink the unit, release its vectors, free it.
    if (Unit->Cores.n_elems == 0)
    {
        Core->Environment->sim_units_manager->rem_sim_unit_from_manager(Unit); // FUN_06349460
        IVP_Simulation_Unit_Free_Vector_Buffers(Unit);
        ::operator delete(Unit); // FUN_06391d5a
    }
}

// ============================================================================
// Controller-bucket build / merge / split (see the header).
// ============================================================================

// FUN_06348860
bool IVP_Simulation_Unit_Has_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller)
{
    for (int i = static_cast<int>(Unit->ControllerBuckets.n_elems) - 1; i >= 0; --i)
    {
        if (Unit->ControllerBuckets.ElementAt(i)->Controller == Controller)
        {
            return true;
        }
    }
    return false;
}

// FUN_06348900
void IVP_Simulation_Unit_Create_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller)
{
    IVP_Sim_Unit_Controller_Bucket* Bucket = new IVP_Sim_Unit_Controller_Bucket();
    Bucket->Controller = Controller;
    Unit->ControllerBuckets.Add(Bucket);
}

// FUN_063488a0. No not-found test: retail indexes elems[-1].
void IVP_Simulation_Unit_Add_Core_To_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller, IVP_Core* Core)
{
    const int32_t Index = IVP_Simulation_Unit_Find_Controller_Bucket_Index(Unit, Controller);
    IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[Index];
    Bucket->Cores.Add(Core);
}

// FUN_063487f0
void IVP_Simulation_Unit_Rebuild_Controller_Buckets(IVP_Simulation_Unit* Unit)
{
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        IVP_Core* Core = Unit->Cores.ElementAt(i);
        for (int j = static_cast<int>(Core->Controllers.n_elems) - 1; j >= 0; --j)
        {
            IVP_Controller* Controller = Core->Controllers.ElementAt(j);

            if (!IVP_Simulation_Unit_Has_Controller_Bucket(Unit, Controller))
            {
                IVP_Simulation_Unit_Create_Controller_Bucket(Unit, Controller);
            }
            IVP_Simulation_Unit_Add_Core_To_Controller_Bucket(Unit, Controller, Core);
        }
    }

    IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority(Unit); // FUN_063492c0
}

// FUN_06348b80. The buckets come from `new` (06348900); FUN_06348be0 (the
// Cores vector's Clear) is the member destructor.
void IVP_Simulation_Unit_Clear_Controller_Buckets(IVP_Simulation_Unit* Unit)
{
    for (int i = static_cast<int>(Unit->ControllerBuckets.n_elems) - 1; i >= 0; --i)
    {
        delete Unit->ControllerBuckets.elems[i];
    }
    Unit->ControllerBuckets.Clear();
}

// FUN_06348ad0 (see the header).
IVP_Core* IVP_Simulation_Unit_Find_Disconnected_Core(IVP_Simulation_Unit* Unit)
{
    // Reset every core's union-find link.
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        Unit->Cores.ElementAt(i)->MergedCoreWhichReplacedIt = nullptr;
    }

    // Union the cores of every bucket's controller (slot 2,
    // get_associated_controlled_cores; count read once). An empty list is
    // skipped before elems[0] is read.
    for (int32_t B = static_cast<int32_t>(Unit->ControllerBuckets.n_elems) - 1; B >= 0; --B)
    {
        IVP_U_Vector<IVP_Core>* const Cores = Unit->ControllerBuckets.elems[B]->Controller->get_associated_controlled_cores();
        const int32_t Count = static_cast<int32_t>(Cores->n_elems);
        if (Count - 1 < 0)
        {
            continue;
        }

        IVP_Core* const FirstMaster = IVP_Core_UnionFindGetFather(Cores->elems[0]);
        for (int32_t J = Count - 1; J >= 0; --J)
        {
            IVP_Core* const Master = IVP_Core_UnionFindGetFather(Cores->elems[J]);
            if (Master != FirstMaster)
            {
                Master->MergedCoreWhichReplacedIt = FirstMaster;
            }
        }
    }

    // The first core's master is taken with no count test (an empty unit
    // reads elems[0], as retail); the first core with another master is
    // returned.
    IVP_Core* const FirstMaster = IVP_Core_UnionFindGetFather(Unit->Cores.elems[0]);
    for (int32_t I = 0; I < static_cast<int32_t>(Unit->Cores.n_elems); ++I)
    {
        IVP_Core* const Master = IVP_Core_UnionFindGetFather(Unit->Cores.elems[I]);
        if (Master != FirstMaster)
        {
            return Master;
        }
    }
    return nullptr;
}

// FUN_06348960. Peels one connected group per pass into a new unit; the
// PendingMaster / NeedsAnotherPass carry across passes, as in retail.
void IVP_Simulation_Unit_Split_Off_Core_Group(IVP_Simulation_Unit* Unit, IVP_Core* TargetMaster)
{
    for (;;)
    {
        IVP_Core* PendingMaster = nullptr; // the first non-matching core's master this pass
        bool NeedsAnotherPass = false;

        IVP_Simulation_Unit* NewUnit = new IVP_Simulation_Unit(); // operator_new(0x24) + FUN_06348e10
        NewUnit->State = (NewUnit->State & 0xFFFFFF01u) | 1u;     // low byte 1 ("awake")

        TargetMaster->Environment->sim_units_manager->add_sim_unit_to_manager(NewUnit); // FUN_06349400

        int Index = 0;
        while (Index < static_cast<int>(Unit->Cores.n_elems))
        {
            IVP_Core* Core = Unit->Cores.ElementAt(Index);
            IVP_Core* CurrentMaster = IVP_Core_UnionFindGetFather(Core);

            if (CurrentMaster == TargetMaster)
            {
                Unit->Cores.RemoveAt(Index);
                --Index; // the array shifted down
                NewUnit->Cores.Add(Core);
                Core->OwningSimUnit = NewUnit;
            }
            else if (PendingMaster == nullptr)
            {
                PendingMaster = CurrentMaster;
            }
            else if (CurrentMaster != PendingMaster)
            {
                // A third group is left behind: one more pass.
                NeedsAnotherPass = true;
            }

            ++Index;
        }

        IVP_Simulation_Unit_Rebuild_Controller_Buckets(NewUnit);

        if (!NeedsAnotherPass)
        {
            break;
        }
        TargetMaster = PendingMaster;
    }
}

// FUN_06348aa0
void IVP_Simulation_Unit_Rebalance(IVP_Simulation_Unit* Unit)
{
    IVP_Core* Disconnected = IVP_Simulation_Unit_Find_Disconnected_Core(Unit);
    if (Disconnected != nullptr)
    {
        IVP_Simulation_Unit_Clear_Controller_Buckets(Unit);
        IVP_Simulation_Unit_Split_Off_Core_Group(Unit, Disconnected);
        IVP_Simulation_Unit_Rebuild_Controller_Buckets(Unit);
    }
}

// FUN_06348c10
void IVP_Simulation_Unit_Absorb_Cores_From(IVP_Simulation_Unit* Unit, IVP_Simulation_Unit* Source)
{
    // Ends holding the last moved core's environment; the count is re-read
    // every pass.
    IVP_Environment* Environment = nullptr;
    for (int32_t I = 0; I < static_cast<int32_t>(Source->Cores.n_elems); ++I)
    {
        IVP_Core* const Core = Source->Cores.elems[I];
        Unit->Cores.Add(Core); // FUN_06348ec0
        Environment = Core->Environment;
        Core->OwningSimUnit = Unit;
    }

    // Reproduced on purpose: no test, so an empty Source dereferences null as
    // retail does.
    Environment->sim_units_manager->rem_sim_unit_from_manager(Source); // FUN_06349460
}

// FUN_06348c60
void IVP_Simulation_Unit_Merge_From(IVP_Simulation_Unit* Unit, IVP_Simulation_Unit* Source)
{
    IVP_Simulation_Unit_Clear_Controller_Buckets(Unit);
    IVP_Simulation_Unit_Absorb_Cores_From(Unit, Source);
    IVP_Simulation_Unit_Rebuild_Controller_Buckets(Unit);
}

// ============================================================================
// Controller detach / per-unit wake / motion classification (see the header).
// ============================================================================

// FUN_06349210: the standalone form of the per-controller body that
// IVP_Simulation_Unit_Detach_Core runs inline. No not-found test (elems[-1]);
// the emptied bucket is freed inline with the same index.
void IVP_Simulation_Unit_Detach_Core_From_Controller_Bucket(IVP_Simulation_Unit* Unit,
                                                              IVP_Controller* Controller,
                                                              IVP_Core* Core)
{
    const int32_t BucketIndex = IVP_Simulation_Unit_Find_Controller_Bucket_Index(Unit, Controller);
    IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[BucketIndex];
    Bucket->Cores.RemoveElement(Core);

    if (Bucket->Cores.n_elems == 0)
    {
        delete Bucket; // Clear 06348be0 + operator delete
        for (int32_t I = BucketIndex; I < static_cast<int32_t>(Unit->ControllerBuckets.n_elems) - 1; ++I)
        {
            Unit->ControllerBuckets.elems[I] = Unit->ControllerBuckets.elems[I + 1];
        }
        --Unit->ControllerBuckets.n_elems;
    }
}

// FUN_063491d0
void IVP_Simulation_Unit_Register_Core_With_Controller(IVP_Simulation_Unit* Unit,
                                                        IVP_Controller* Controller,
                                                        IVP_Core* Core)
{
    if (!IVP_Simulation_Unit_Has_Controller_Bucket(Unit, Controller))
    {
        IVP_Simulation_Unit_Create_Controller_Bucket(Unit, Controller);
    }
    IVP_Simulation_Unit_Add_Core_To_Controller_Bucket(Unit, Controller, Core);
    IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority(Unit);
}

// FUN_06349350: swaps ControllerBuckets.elems[i] and [j].
void IVP_Simulation_Unit_Swap_Controller_Buckets(IVP_Simulation_Unit* Unit, int i, int j)
{
    IVP_Sim_Unit_Controller_Bucket* Bi = Unit->ControllerBuckets.ElementAt(i);
    IVP_Sim_Unit_Controller_Bucket* Bj = Unit->ControllerBuckets.ElementAt(j);
    Unit->ControllerBuckets.elems[i] = Bj;
    Unit->ControllerBuckets.elems[j] = Bi;
}

// FUN_063492c0: insertion sort by get_controller_priority() (signed). When
// [i],[i+1] are out of order they swap, then the moved element bubbles back
// while still greater than its left neighbour.
void IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority(IVP_Simulation_Unit* Unit)
{
    int Count = static_cast<int>(Unit->ControllerBuckets.n_elems);
    for (int i = 0; i + 1 < Count; ++i)
    {
        int j = i + 1;
        int PriorityJ = Unit->ControllerBuckets.ElementAt(j)->Controller->get_controller_priority();
        int PriorityI = Unit->ControllerBuckets.ElementAt(i)->Controller->get_controller_priority();

        if (PriorityI <= PriorityJ)
        {
            continue;
        }

        IVP_Simulation_Unit_Swap_Controller_Buckets(Unit, i, j);

        for (int k = i; k > 0; --k)
        {
            int PriorityPrev = Unit->ControllerBuckets.ElementAt(k - 1)->Controller->get_controller_priority();
            if (PriorityPrev <= PriorityJ)
            {
                break;
            }
            IVP_Simulation_Unit_Swap_Controller_Buckets(Unit, k - 1, k);
        }
    }
}

// FUN_063494f0: the same loop IVP_Core::ensure_in_simulation inlines.
void IVP_Simulation_Unit_Update_Cores_Last_Motion_Time(IVP_Simulation_Unit* Unit)
{
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        Unit->Cores.ElementAt(i)->update_time_of_last_motion(); // FUN_063470e0
    }
}

// FUN_06349490, the library's sim_unit_revive_for_simulation (the one port of
// it; IVP_Core::revive_sleeping_core forwards here). No null tests; the sleep
// test is State's low byte.
void IVP_Simulation_Unit_Revive_If_Sleeping(IVP_Simulation_Unit* Unit, IVP_Environment* Environment)
{
    // Scan the cores backwards (count re-read only when the scan restarts); a
    // core whose signed movement byte is >= 8 gets revive_pass (0634b480), and
    // a return of exactly 1 restarts the scan.
    int32_t Index = static_cast<int32_t>(Unit->Cores.n_elems);
    for (;;)
    {
        --Index;
        if (Index < 0)
        {
            break;
        }
        IVP_Core* const Core = Unit->Cores.elems[Index];
        if (IVP_SignedLowByte(Core->MovementState) < 8)
        {
            continue;
        }
        if (Core->revive_pass() == 1)
        {
            Index = static_cast<int32_t>(Unit->Cores.n_elems);
        }
    }

    if (static_cast<uint8_t>(Unit->State) == 0x08)
    {
        Environment->sim_units_manager->rem_sim_unit_from_manager(Unit); // 06349460
        Unit->State = (Unit->State & 0xFFFFFF01u) | 0x1u;
        Environment->sim_units_manager->add_sim_unit_to_manager(Unit);   // 06349400
    }
}


// FUN_063495e0 (see the header). Each core's classify_motion_state result
// (1/2/3) goes into its MovementState low byte and is ANDed into Verdict,
// which starts at 3 ("long idle"): only if every core reports 3 does the unit
// go to sleep. An empty unit therefore always sleeps, as in retail.
int IVP_Simulation_Unit_Update_Motion_And_Maybe_Sleep(IVP_Simulation_Unit* Unit, IVP_Environment* Environment)
{
    const float TimeBase = Environment->current_time.time_base;
    const float TimeOffset = Environment->current_time.time_offset;

    int Verdict = 3;
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        IVP_Core* Core = Unit->Cores.ElementAt(i);
        int Result = Core->classify_motion_state(TimeBase, TimeOffset); // FUN_063478f0
        Core->MovementState = (Core->MovementState & ~0xFFu) | (static_cast<uint32_t>(Result) & 0xFFu);
        Verdict &= Result;
    }

    if (Verdict != 3)
    {
        return 0;
    }

    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        Unit->Cores.ElementAt(i)->freeze_and_fire_object_frozen(); // FUN_0634b5e0
    }

    IVP_Sim_Units_Manager* const Manager = Environment->sim_units_manager;
    Manager->rem_sim_unit_from_manager(Unit); // 06349460
    Unit->State = (Unit->State & 0xFFFFFF08u) | 0x08u;
    Manager->add_sim_unit_to_manager(Unit);   // 06349400

    return 1;
}

// FUN_063495c0
void IVP_Simulation_Unit_Rebalance_And_Clear_Topology_Flag(IVP_Simulation_Unit* Unit)
{
    IVP_Simulation_Unit_Clear_Controller_Buckets(Unit);  // FUN_06348b80
    IVP_Simulation_Unit_Rebuild_Controller_Buckets(Unit); // FUN_063487f0
    IVP_Simulation_Unit_Rebalance(Unit);                   // FUN_06348aa0
    Unit->State &= 0xFFFFFCFFu; // clears bits 0x300
}

// FUN_063496a0, the library's simulate_single_sim_unit_psi. x87 terms are
// double and rounded at the float stores.
//   - The arena busy word (vector_cores_frozen +0x10) is incremented; the
//     current time and the controller bucket count are read once up front.
//   - Cores: retail runs n-1 .. 1 in the loop and then index 0
//     unconditionally, so an empty unit reads elems[0], as retail does.
//   - Verdict = OR of the float bits of (1 - |speed|^2); a negative result
//     (some core faster than 1) takes the first arm.
//   - Every controller bucket, last first: slot 4 do_simulation_controller.
//   - Every core appended to OutputCoreList, last first.
//   - Cooldown expired -> 063495e0. Every object of every core, last first:
//     recalc_invalid_mindists (06345460). State & 0x300 -> 063495c0.
//   - The busy word is decremented and the arena rewound (06344070).
// The per-core body runs twice in retail (loop and index 0); returns the float
// bits of 1 - |speed|^2.
static uint32_t IVP_Simulation_Unit_Psi_Core_Step(IVP_Core* Core, float TimeBase, float TimeOffset)
{
    const float Alpha = (float)((((double)(TimeBase)
        - *reinterpret_cast<const float*>(&Core->TimeOfLastPsi.time_base)) + TimeOffset)
        - *reinterpret_cast<const float*>(&Core->TimeOfLastPsi.time_offset));

    Core->QWorldFCoreNext.set_matrix(&Core->MWorldFCore); // FUN_06350bc0 (+0xe4 -> +0xf4)

    // FUN_0628be60: one rounding per component.
    Core->MWorldFCore.vv.set(
        (float)((double)(Alpha) * Core->DeltaWorldFCore.x + Core->PosWorldFCore.x),
        (float)((double)(Alpha) * Core->DeltaWorldFCore.y + Core->PosWorldFCore.y),
        (float)((double)(Alpha) * Core->DeltaWorldFCore.z + Core->PosWorldFCore.z));

    Core->commit_speed_changes(); // FUN_06346dd0
    Core->MovementState &= 0xFFFF00FFu; // +0x60
    Core->PendingImpactCount = 0;       // +0x64 (word)

    // 1.0f is DAT_063a53d0.
    const double SpeedMagSq = ((double)(Core->Speed.x) * Core->Speed.x
        + (double)(Core->Speed.y) * Core->Speed.y)
        + (double)(Core->Speed.z) * Core->Speed.z;
    const float SignTerm = (float)(1.0 - SpeedMagSq);
    uint32_t SignBits;
    std::memcpy(&SignBits, &SignTerm, sizeof(SignBits));
    return SignBits;
}

void IVP_Simulation_Unit_Process_One_Psi_Step(IVP_Event_Sim* EventSim, IVP_Simulation_Unit* Unit, IVP_U_Vector<IVP_Core>* OutputCoreList)
{
    EventSim->sim_unit = Unit;

    IVP_Environment* Env = EventSim->environment;
    // The two counters live on different objects: the arena's own +0x10 busy
    // word and the environment's next_movement_check (+0x118).
    uint16_t* BusyCounter = &Env->vector_cores_frozen->State;
    int16_t* CooldownTimer = &Env->next_movement_check;

    ++(*BusyCounter);

    const float TimeBase = Env->current_time.time_base;
    const float TimeOffset = Env->current_time.time_offset;

    // Captured before anything below runs.
    const int32_t ControllerBucketCount = static_cast<int32_t>(Unit->ControllerBuckets.n_elems);

    uint32_t Verdict = 0;
    for (int32_t I = static_cast<int32_t>(Unit->Cores.n_elems) - 1; I >= 1; --I)
    {
        Verdict |= IVP_Simulation_Unit_Psi_Core_Step(Unit->Cores.elems[I], TimeBase, TimeOffset);
    }
    // Index 0 unconditionally (an empty unit reads elems[0], as retail).
    Verdict |= IVP_Simulation_Unit_Psi_Core_Step(Unit->Cores.elems[0], TimeBase, TimeOffset);

    bool CooldownExpired;
    if (static_cast<int32_t>(Verdict) < 0)
    {
        Unit->State = (Unit->State & 0xFFFFF7FFu) | 0x400u;
        --(*CooldownTimer);
        if (*CooldownTimer == 0)
        {
            // * -5.0f (DAT_063a549c) on the unrounded value, ftol (truncation),
            // 0xf - result.
            const double RandomValue = (double)(IVP_FastRandomFloat()) * -5.0f;
            const int32_t RandomInt = static_cast<int32_t>(RandomValue);
            *CooldownTimer = static_cast<int16_t>(0xF - RandomInt);
            CooldownExpired = true;
        }
        else
        {
            CooldownExpired = false;
        }
    }
    else
    {
        uint32_t StateVerdict = Unit->State;
        const uint32_t Shifted = static_cast<uint32_t>(static_cast<int32_t>(StateVerdict << 0x14) >> 0x12);
        StateVerdict = ((Shifted ^ StateVerdict) & 0x3000u) ^ StateVerdict;
        Unit->State = StateVerdict;
        if ((StateVerdict & 0x3000u) != 0)
        {
            IVP_Simulation_Unit_Update_Cores_Last_Motion_Time(Unit); // FUN_063494f0
        }
        Unit->State &= 0xFFFFF3FFu;

        --(*CooldownTimer);
        if (*CooldownTimer == 0)
        {
            // Same reseed as above.
            const double RandomValue = (double)(IVP_FastRandomFloat()) * -5.0f;
            const int32_t RandomInt = static_cast<int32_t>(RandomValue);
            *CooldownTimer = static_cast<int16_t>(0xF - RandomInt);
            CooldownExpired = true;
        }
        else
        {
            CooldownExpired = false;
        }
    }

    for (int32_t B = ControllerBucketCount - 1; B >= 0; --B)
    {
        IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[B];
        Bucket->Controller->do_simulation_controller(EventSim, &Bucket->Cores); // slot 4
    }

    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        OutputCoreList->Add(Unit->Cores.ElementAt(i));
    }

    if (CooldownExpired)
    {
        IVP_Simulation_Unit_Update_Motion_And_Maybe_Sleep(Unit, Env); // FUN_063495e0
    }

    for (int32_t I = static_cast<int32_t>(Unit->Cores.n_elems) - 1; I >= 0; --I)
    {
        IVP_Core* const Core = Unit->Cores.elems[I];
        for (int32_t J = static_cast<int32_t>(Core->Objects.n_elems) - 1; J >= 0; --J)
        {
            Core->Objects.elems[J]->recalc_invalid_mindists(); // FUN_06345460
        }
    }

    if ((Unit->State & 0x300u) != 0)
    {
        IVP_Simulation_Unit_Rebalance_And_Clear_Topology_Flag(Unit); // FUN_063495c0
    }

    --(*BusyCounter);
    Env->vector_cores_frozen->reset_chunks(); // FUN_06344070
}

// ============================================================================
// FUN_063499c0: IVP_Sim_Units_Manager::simulate_sim_units_psi. Builds the
// IVP_Event_Sim (sim_unit is set per unit) and runs FUN_063496a0 over the
// active list in retail's register-pipeline order: the next three units are
// fetched before each call, and the last three run newest first -- for units
// A,B,C,D,E the order is A, B, E, D, C.
// ============================================================================
void IVP_Sim_Units_Manager::simulate_sim_units_psi(IVP_Environment* Environment,
                                                   IVP_U_Vector<IVP_Core>* CoresWhichNeedCalcNextPsi)
{
    IVP_Event_Sim EventSim;
    EventSim.delta_time = Environment->delta_time;
    EventSim.i_delta_time = Environment->inv_delta_time;
    EventSim.environment = Environment;

    IVP_Simulation_Unit* Unit0 = sim_units_head;
    if (Unit0 == nullptr)
    {
        return;
    }
    IVP_Simulation_Unit* Unit1 = Unit0->Next;
    if (Unit1 != nullptr)
    {
        IVP_Simulation_Unit* Unit2 = Unit1->Next;
        if (Unit2 != nullptr)
        {
            IVP_Simulation_Unit* Unit3 = Unit2->Next;
            if (Unit3 != nullptr)
            {
                do
                {
                    IVP_Simulation_Unit_Process_One_Psi_Step(&EventSim, Unit0, CoresWhichNeedCalcNextPsi);
                    Unit0 = Unit1;
                    Unit1 = Unit2;
                    Unit2 = Unit3;
                    Unit3 = Unit3->Next;
                } while (Unit3 != nullptr);
            }
            IVP_Simulation_Unit_Process_One_Psi_Step(&EventSim, Unit2, CoresWhichNeedCalcNextPsi);
        }
        IVP_Simulation_Unit_Process_One_Psi_Step(&EventSim, Unit1, CoresWhichNeedCalcNextPsi);
    }
    IVP_Simulation_Unit_Process_One_Psi_Step(&EventSim, Unit0, CoresWhichNeedCalcNextPsi);
}
