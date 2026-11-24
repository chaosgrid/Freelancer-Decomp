// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Implementation for ivp_listmanager_topology.h. FUN_06355c10 and FUN_063562d0
// are in ivp_mindist_limiter.cpp.
#include "ivp_listmanager_topology.h"
#include "../ivp_physics/ivp_real_object.h"
#include "../ivp_physics/ivp_environment.h"
#include "../ivp_utility/ivp_math.h"    // IVP_U_Matrix3::vmult/vimult
#include "../ivp_compact_builder/ivp_compact_ledge.h" // IVP_Compact_Edge::GetCompactLedge
#include "../ivp_utility/ivp_core_trap.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "../ivp_physics/ivp_cluster_manager.h" // IVP_Cluster_Manager::fire_event_friction_deleted (0634b140)

namespace
{
    // The event FUN_06355200 builds on its own stack (frame offset +0x10 after
    // the four pushes) and hands to the collision listeners. Only these fields
    // are written (06355261-06355284); retail leaves the rest as stack
    // contents, the port zeroes them. The block at +0x0c is what +0x04 points
    // at. PORT-LOCAL name (Ipion: an IVP_Event_Friction with its contact
    // situation embedded).
    struct FIVP_ContactPointDeletedEvent
    {
        IVP_Environment* Environment;           // +0x00
        void* Situation;                        // +0x04: = &SituationBlock
        IVP_Contact_Point* ContactPoint;   // +0x08
        uint8_t SituationBlock[0x30];           // +0x0c: not written here
        IVP_Real_Object* Objects[2];            // +0x3c / +0x40
        void* CompactEdges[2];                  // +0x44 / +0x48: Feature1 / Feature2
    };
    static_assert(offsetof(FIVP_ContactPointDeletedEvent, Objects) == 0x3c, "event +0x3c");
    static_assert(offsetof(FIVP_ContactPointDeletedEvent, CompactEdges) == 0x44, "event +0x44");
}

// ---------------------------------------------------------------------
// FUN_06355200, the contact point's destructor body (called from FUN_06355490
// before operator delete). Releases each feature's ledge through
// surface_manager slot 8, fires FUN_0634cfe0 and, for each object with flag
// 0x2000, FUN_0634b140 -- both on Object1's environment -- then unlinks both
// embedded list nodes (undoing IVP_Contact_Point's ctor insert).
// ---------------------------------------------------------------------
void IVP_UnlinkPairRecordFromObjects(IVP_Contact_Point* Record)
{
    IVP_Real_Object* Object1 = Record->Object1;
    IVP_Real_Object* Object2 = Record->Object2;
    IVP_Environment* Environment = Object1->environment;

    Object1->surface_manager->remove_reference_to_ledge(
        reinterpret_cast<IVP_Compact_Edge*>(Record->Feature1)->GetCompactLedge());
    Object2->surface_manager->remove_reference_to_ledge(
        reinterpret_cast<IVP_Compact_Edge*>(Record->Feature2)->GetCompactLedge());

    FIVP_ContactPointDeletedEvent Event;
    std::memset(&Event, 0, sizeof(Event));
    Event.Environment = Environment;
    Event.Situation = Event.SituationBlock;
    Event.ContactPoint = Record;
    Event.Objects[0] = Object1;
    Event.Objects[1] = Object2;
    Event.CompactEdges[0] = Record->Feature1;
    Event.CompactEdges[1] = Record->Feature2;

    IVP_Environment_Notify_Friction_Deleted(Environment, &Event);

    if ((Object1->flags & 0x2000u) != 0)
    {
        Environment->cluster_manager->fire_event_friction_deleted(Object1, &Event); // 0634b140
    }
    if ((Object2->flags & 0x2000u) != 0)
    {
        Environment->cluster_manager->fire_event_friction_deleted(Object2, &Event); // 0634b140
    }

    // Node #1 (Next1/Prev1, +0x08/+0x0c) from Object1->mindist_list.
    if (Record->Next1 != nullptr)
        *reinterpret_cast<void**>(static_cast<char*>(Record->Next1) + 0x4) = Record->Prev1;

    if (Record->Prev1 != nullptr)
        *reinterpret_cast<void**>(Record->Prev1) = Record->Next1;
    else
        Record->Object1->mindist_list = Record->Next1;

    // Node #2 (Next2/Prev2, +0x1c/+0x20) from Object2->mindist_list.
    if (Record->Next2 != nullptr)
        *reinterpret_cast<void**>(static_cast<char*>(Record->Next2) + 0x4) = Record->Prev2;

    if (Record->Prev2 != nullptr)
        *reinterpret_cast<void**>(Record->Prev2) = Record->Next2;
    else
        Record->Object2->mindist_list = Record->Next2;
}

// ---------------------------------------------------------------------------
// FUN_063557e0
// ---------------------------------------------------------------------------
// Same four phases as IVP_Simulation_Unit_Find_Disconnected_Core, keyed by
// CorePairEntries. Phases 3/4 iterate backward and overwrite the running
// result on every match (no early exit), so with 3+ disconnected groups the
// returned core differs from a forward-first-mismatch search.
IVP_Core* IVP_ListManager_FindDisconnectedCore(IVP_ListManager* Manager)
{
    // Phase 1: reset union-find over every registered Core.
    for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
        Manager->RegisteredCores.ElementAt(i)->MergedCoreWhichReplacedIt = nullptr;

    // Phase 2: rebuild groups from CorePairEntries, skipping entries with a
    // kinematic side (Flags & IVP_CORE_PHYSICAL_UNMOVEABLE).
    for (int i = Manager->CorePairEntries.Len() - 1; i >= 0; --i)
    {
        IVP_ListManager_CorePairEntry* Entry = Manager->CorePairEntries.ElementAt(i);
        IVP_Core* CoreA = Entry->CoreA;
        IVP_Core* CoreB = Entry->CoreB;
        if ((CoreA->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
            continue;
        if ((CoreB->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
            continue;

        IVP_Core* MasterA = IVP_Core_UnionFindGetFather(CoreA);
        IVP_Core* MasterB = IVP_Core_UnionFindGetFather(CoreB);
        if (MasterA != MasterB)
            MasterB->MergedCoreWhichReplacedIt = MasterA;
    }

    // Phase 3: reference master = master of the lowest-index non-kinematic
    // registered Core.
    IVP_Core* ReferenceMaster = nullptr;
    for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
    {
        IVP_Core* Core = Manager->RegisteredCores.ElementAt(i);
        if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
            ReferenceMaster = IVP_Core_UnionFindGetFather(Core);
    }

    // Phase 4: any non-kinematic Core whose master differs.
    IVP_Core* Disconnected = nullptr;
    for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
    {
        IVP_Core* Core = Manager->RegisteredCores.ElementAt(i);
        if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
            continue;
        IVP_Core* Master = IVP_Core_UnionFindGetFather(Core);
        if (Master != ReferenceMaster)
            Disconnected = Master;
    }
    return Disconnected;
}

namespace
{
    // The per-core record of a friction system is IVP_Friction_Info_For_Core
    // (ivp_mindist.h, operator_new(0xc) at .text:063558fd).
    IVP_Friction_Info_For_Core* IVP_FindFrictionInfo(IVP_Core* Core, IVP_ListManager* FrictionSystem)
    {
        return static_cast<IVP_Friction_Info_For_Core*>(
            Core->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(FrictionSystem))));
    }
}

// ---------------------------------------------------------------------------
// FUN_06355890
// ---------------------------------------------------------------------------
// The ListManager twin of IVP_Simulation_Unit_Split_Off_Core_Group, using the
// same IVP_Core object-hash-entry calls as IVP_MergeRegisteredCores
// (ivp_mindist_limiter.cpp). Retail's outer goto-loop (`JMP LAB_063558ae`)
// is the for(;;):
//   1. NewManager = new IVP_ListManager(Manager->Environment).
//   2. For every Core in Manager->RegisteredCores: a kinematic Core gets a
//      fresh IVP_Friction_Info_For_Core keyed on NewManager and is registered
//      there too (it can be tracked by both managers); otherwise a Core of
//      TargetMaster's group moves to NewManager and its hash entry is re-keyed
//      in place.
//   3. For every CorePairEntry whose (post-kinematic-normalized) side
//      resolves to TargetMaster's group: move the entry itself from
//      Manager->CorePairEntries to NewManager->CorePairEntries, and for each
//      of its AttachedCtx pair-records, move it from Manager's Ctx list to
//      NewManager's (RemoveCtx/InsertCtx) and, if the pair straddles a
//      kinematic Core, move the record between that Core's two hash
//      entries too.
//   4. Cleanup pass: any kinematic Core left with an EMPTY hash entry under
//      either manager gets that entry released and is unregistered from
//      that specific manager.
//   5. If both managers still have >= 2 RegisterCore calls, look for a further
//      disconnected group in Manager and loop with it; otherwise the underfull
//      manager's sole core releases its record and that manager is deleted
//      (`vtbl[6](1)`, :06355b39 for NewManager, :06355b8a for Manager -- the
//      caller's own friction system).
//
// DEVIATION (retail double free): both underfull branches call
// release_object_hash_entry(H) (FUN_06347d50: unregister, Clear, delete) and
// then run Clear + delete on H a second time. The port releases once.
void IVP_ListManager_SplitOffCoreGroup(IVP_ListManager* Manager, IVP_Core* TargetMaster)
{
    for (;;)
    {
        // operator_new(0x4c) + FUN_0634b600.
        IVP_ListManager* NewManager = new IVP_ListManager(Manager->Environment);

        // Step 2: move/register Cores.
        for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
        {
            IVP_Core* Core = Manager->RegisteredCores.ElementAt(i);

            if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
            {
                // Kinematic: tracked on NewManager too, regardless of group
                // (it never enters the union-find graph).
                IVP_Friction_Info_For_Core* Handle = new IVP_Friction_Info_For_Core();
                Handle->FrictionSystem = NewManager;
                NewManager->RegisterCore(Core);
                Core->register_object_hash_entry(Handle); // FUN_06347c80
            }
            else if (IVP_Core_UnionFindGetFather(Core) == TargetMaster)
            {
                Manager->UnregisterCore(Core);
                NewManager->RegisterCore(Core);

                // Direct key overwrite (:06355958), not the unregister/
                // rewrite/register IVP_MergeRegisteredCores uses.
                IVP_FindFrictionInfo(Core, Manager)->FrictionSystem = NewManager;
            }
        }

        // Step 3: move CorePairEntries of TargetMaster's group and their
        // attached records.
        for (int i = Manager->CorePairEntries.Len() - 1; i >= 0; --i)
        {
            IVP_ListManager_CorePairEntry* Entry = Manager->CorePairEntries.ElementAt(i);
            IVP_Core* PickA = Entry->CoreA;
            IVP_Core* PickB = Entry->CoreB;

            // PickA ends up as the non-kinematic union-find probe.
            bool EitherKinematic = false;
            IVP_Core* KinematicSide = nullptr;
            if ((PickA->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
            {
                EitherKinematic = true;
                KinematicSide = PickA;
                std::swap(PickA, PickB); // PickA (probe) = old PickB
            }
            else if ((PickB->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
            {
                EitherKinematic = true;
                KinematicSide = PickB;
            }

            IVP_Friction_Info_For_Core* KinHandleUnderManager = nullptr;
            IVP_Friction_Info_For_Core* KinHandleUnderNewManager = nullptr;
            if (EitherKinematic)
            {
                KinHandleUnderManager = IVP_FindFrictionInfo(KinematicSide, Manager);
                KinHandleUnderNewManager = IVP_FindFrictionInfo(KinematicSide, NewManager);
            }

            if (IVP_Core_UnionFindGetFather(PickA) != TargetMaster)
                continue;

            Manager->CorePairEntries.RemoveElement(Entry);
            NewManager->CorePairEntries.Add(Entry);

            for (int j = Entry->AttachedCtx.Len() - 1; j >= 0; --j)
            {
                IVP_Contact_Point* Node = Entry->AttachedCtx.ElementAt(j);

                // Retail's assert (:063559ed): walk the whole dist list for
                // Node and trap with a null store when it is absent.
                bool bFound = false;
                for (IVP_Contact_Point* Walk = Manager->CtxListHead; Walk != nullptr; Walk = Walk->ListManagerListNext)
                {
                    if (Walk == Node)
                        bFound = true;
                }
                if (!bFound)
                {
                    IVP_CoreTrap();
                }

                Manager->RemoveCtx(Node);    // FUN_0634b8d0
                NewManager->InsertCtx(Node); // FUN_0634b9a0

                if (KinHandleUnderManager != nullptr)
                {
                    KinHandleUnderManager->FrDists.RemoveElement(Node); // FUN_06355310
                    KinHandleUnderNewManager->FrDists.Add(Node);        // FUN_063556b0
                }
            }
        }

        // Step 4: release now-empty kinematic hash entries and unregister the
        // Core from whichever manager it emptied out under.
        for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
        {
            IVP_Core* Core = Manager->RegisteredCores.ElementAt(i);
            if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
                continue;

            IVP_Friction_Info_For_Core* NewManagerHandle = IVP_FindFrictionInfo(Core, NewManager);
            IVP_Friction_Info_For_Core* ManagerHandle = IVP_FindFrictionInfo(Core, Manager);

            if (NewManagerHandle->FrDists.n_elems == 0) // FUN_06355370
            {
                Core->release_object_hash_entry(NewManagerHandle);
                NewManager->UnregisterCore(Core);
            }
            if (ManagerHandle->FrDists.n_elems == 0)
            {
                Core->release_object_hash_entry(ManagerHandle);
                Manager->UnregisterCore(Core);
            }
        }

        // Step 5. Both counts are compared as signed words.
        if (static_cast<int16_t>(NewManager->RegisterCoreCallCount) < 2)
        {
            // elems[0] is read without a count check; released once (see
            // DEVIATION above).
            IVP_Core* SoleCore = NewManager->RegisteredCores.elems[0];
            SoleCore->release_object_hash_entry(IVP_FindFrictionInfo(SoleCore, NewManager));
            delete NewManager;
            return;
        }
        if (static_cast<int16_t>(Manager->RegisterCoreCallCount) < 2)
        {
            // The same on Manager -- the caller's system.
            IVP_Core* SoleCore = Manager->RegisteredCores.elems[0];
            SoleCore->release_object_hash_entry(IVP_FindFrictionInfo(SoleCore, Manager));
            delete Manager;
            return;
        }

        // FUN_063557e0 on Manager; none -> done, else loop.
        IVP_Core* NextDisconnected = IVP_ListManager_FindDisconnectedCore(Manager);
        if (NextDisconnected == nullptr)
            return;
        TargetMaster = NextDisconnected;
    }
}

// LAB_06355bb0 is slot 3 of the friction system's vtable (.rdata:063a54d4):
// IVP_ListManager::reset_time in ivp_friction_solver.cpp.

// ---------------------------------------------------------------------------
// FUN_06355c80
// ---------------------------------------------------------------------------
void IVP_ListManager_TickAllCorePairBudgets(IVP_ListManager* Manager)
{
    for (int i = Manager->CorePairEntries.Len() - 1; i >= 0; --i)
        IVP_ListManager_TickCorePairBudget(Manager->CorePairEntries.ElementAt(i));
}

// ---------------------------------------------------------------------------
// FUN_06355ca0
// ---------------------------------------------------------------------------
void IVP_ListManager_ClearAllCorePairBudgets(IVP_ListManager* Manager)
{
    for (int i = Manager->CorePairEntries.Len() - 1; i >= 0; --i)
        Manager->CorePairEntries.ElementAt(i)->IntegratedAntiEnergy = 0;
}

// ---------------------------------------------------------------------------
// FUN_06355cc0
// ---------------------------------------------------------------------------
void IVP_ListManager_TickCorePairBudget(IVP_ListManager_CorePairEntry* Entry)
{
    IVP_Environment* Env = Entry->CoreA->Environment;
    // The product stays on the x87 stack; both stores are rounded copies.
    const double ScaledBudget = (double)(Env->integrated_energy_damp) * Entry->IntegratedAntiEnergy;
    const float ScaledBudgetF = (float)(ScaledBudget);
    Entry->IntegratedAntiEnergy = ScaledBudgetF;

    // Unrounded compare; less or unordered returns.
    if (!(ScaledBudget >= 0.0))
        return;
    if (Entry->CoreA->AxisLock != nullptr)
        return;
    if (Entry->CoreB->AxisLock != nullptr)
        return;

    // FUN_063561c0 returns a float, so the two float updates below round like
    // the x87.
    const float Consumed = IVP_ComputeAndApplyContactAxisImpulse(Entry, ScaledBudgetF);

    float* EnvTickAccumulator = reinterpret_cast<float*>(&Env->manager_context_68[0]); // IVP_Environment+0x68
    *EnvTickAccumulator += Consumed;
    Entry->IntegratedAntiEnergy -= Consumed;
}

// ---------------------------------------------------------------------------
// FUN_06355d20. Everything on the x87 stack.
// ---------------------------------------------------------------------------
double IVP_SolveContactAxisRoot(float A, float B, float C, float D)
{
    const double Sum = (double)(B) + C;
    const double Inner = (double)(A) * A - ((double)(D) + D) * Sum;
    const double Root = A - std::sqrt(std::fabs(Inner));
    return Root * (1.0 / Sum);
}

// ---------------------------------------------------------------------------
// FUN_06355d60, cdecl. float(X*X*C) is spilled into E's argument slot and
// read back; everything else stays on the x87 stack. 0.5f is DAT_063a5460.
// ---------------------------------------------------------------------------
double IVP_SolveContactAxisEnergySplit(float A, float B, float C, float D, float E)
{
    const double T = A / ((double)(D) + E);
    const double X = A - T * E;
    const float XSquaredC = (float)(X * X * C);
    const double Y = T * D;
    const double Difference = (double)(A) * A * C - Y * Y * B;
    return (Difference - XSquaredC) * 0.5f;
}

// ---------------------------------------------------------------------------
// FUN_06355db0, thiscall on the 0x6c-byte scratch, RET 8.
// ---------------------------------------------------------------------------
//   1. A kinematic CoreB argument ((Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) or (Flags & IVP_CORE_PINNED)) is
//      swapped into CoreA (+0x60).
//   2. LinearAxis = normalised (CY.Speed - CX.Speed) (FUN_06347e90),
//      LinearSpeed = its length.
//   3-4. SpinSpeed = length of the normalised world spin delta
//      (MWorldFCore vmult RotSpeed, FUN_06348580).
//   5. SpinAxisA = CX vimult delta; SpinAxisB = CY vimult -delta
//      (FUN_06366010).
//   6. Per side, the length of RotInertia * axis; below 1e-10f (DAT_063a53d8)
//      or unordered, both the length and its inverse slot become 1.0f.
//   7. Masses / inverse masses; a kinematic CX takes CY's scaled by
//      10000.0f / 1e-4f (DAT_063a565c / DAT_063a547c), and so do its
//      rotational length slots.
void IVP_PopulateContactAxisSetup(IVP_PairContactAxisSetup* Setup, IVP_Core* CoreA, IVP_Core* CoreB)
{
    constexpr float kLengthEpsilon = 1e-10f; // DAT_063a53d8 (0x2edbe6ff)
    constexpr float kNegOne = -1.0f;         // DAT_063a5498
    constexpr float kTenThousand = 10000.0f; // DAT_063a565c
    constexpr float kSmallScale = 1e-4f;     // DAT_063a547c (0x38d1b717)

    // Step 1. Retail tests `AL & 0xc` and `AH & 0x3` separately.
    if ((CoreB->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0 || (CoreB->Flags & IVP_CORE_PINNED) != 0)
    {
        Setup->CoreA = CoreB;
        Setup->CoreB = CoreA;
    }
    else
    {
        Setup->CoreA = CoreA;
        Setup->CoreB = CoreB;
    }
    IVP_Core* CX = Setup->CoreA;
    IVP_Core* CY = Setup->CoreB;

    // Step 2. Pad03 is not written.
    IVP_U_Point SpeedDelta{};
    SpeedDelta.k[0] = CY->Speed.x - CX->Speed.x;
    SpeedDelta.k[1] = CY->Speed.y - CX->Speed.y;
    SpeedDelta.k[2] = CY->Speed.z - CX->Speed.z;
    const double SpeedDeltaLength = SpeedDelta.real_length_plus_normize();
    Setup->LinearAxis[0] = SpeedDelta.k[0];
    Setup->LinearAxis[1] = SpeedDelta.k[1];
    Setup->LinearAxis[2] = SpeedDelta.k[2];
    Setup->LinearSpeed = (float)(SpeedDeltaLength);

    // Step 3.
    IVP_U_Point RotSpeedX{};
    RotSpeedX.set(CX->RotSpeed.x, CX->RotSpeed.y, CX->RotSpeed.z);
    IVP_U_Point RotSpeedY{};
    RotSpeedY.set(CY->RotSpeed.x, CY->RotSpeed.y, CY->RotSpeed.z);
    IVP_U_Point WorldSpinX{};
    IVP_U_Point WorldSpinY{};
    CX->MWorldFCore.vmult(&RotSpeedX, &WorldSpinX);
    CY->MWorldFCore.vmult(&RotSpeedY, &WorldSpinY);

    // Step 4.
    IVP_U_Point SpinDelta{};
    SpinDelta.k[0] = WorldSpinY.k[0] - WorldSpinX.k[0];
    SpinDelta.k[1] = WorldSpinY.k[1] - WorldSpinX.k[1];
    SpinDelta.k[2] = WorldSpinY.k[2] - WorldSpinX.k[2];
    const double SpinDeltaLength = SpinDelta.real_length_plus_normize();
    Setup->SpinSpeed = (float)(SpinDeltaLength);

    // Step 5. The negation is a multiply by DAT_063a5498 (-1.0f).
    IVP_U_Point SpinAxisX{};
    CX->MWorldFCore.vimult(&SpinDelta, &SpinAxisX);
    SpinDelta.k[0] = SpinDelta.k[0] * kNegOne;
    SpinDelta.k[1] = SpinDelta.k[1] * kNegOne;
    SpinDelta.k[2] = SpinDelta.k[2] * kNegOne;
    IVP_U_Point SpinAxisY{};
    CY->MWorldFCore.vimult(&SpinDelta, &SpinAxisY);
    Setup->SpinAxisA[0] = SpinAxisX.k[0]; Setup->SpinAxisA[1] = SpinAxisX.k[1]; Setup->SpinAxisA[2] = SpinAxisX.k[2];
    Setup->SpinAxisB[0] = SpinAxisY.k[0]; Setup->SpinAxisB[1] = SpinAxisY.k[1]; Setup->SpinAxisB[2] = SpinAxisY.k[2];

    // Step 6. The weighted components are floats; L (FUN_06347e60) stays
    // unrounded for the compare and the inverse.
    IVP_U_Point WeightedX{};
    WeightedX.k[0] = CX->RotInertia.x * Setup->SpinAxisA[0];
    WeightedX.k[1] = CX->RotInertia.y * Setup->SpinAxisA[1];
    WeightedX.k[2] = CX->RotInertia.z * Setup->SpinAxisA[2];
    const double LengthX = WeightedX.real_length();
    Setup->AxisInertiaA = (float)(LengthX);
    if (!(LengthX >= kLengthEpsilon))
    {
        Setup->AxisInertiaA = 1.0f;
        Setup->InvAxisInertiaA = 1.0f;
    }
    else
    {
        Setup->InvAxisInertiaA = (float)(1.0 / LengthX);
    }

    IVP_U_Point WeightedY{};
    WeightedY.k[0] = Setup->SpinAxisB[0] * CY->RotInertia.x;
    WeightedY.k[1] = CY->RotInertia.y * Setup->SpinAxisB[1];
    WeightedY.k[2] = CY->RotInertia.z * Setup->SpinAxisB[2];
    const double LengthY = WeightedY.real_length();
    Setup->AxisInertiaB = (float)(LengthY);
    if (!(LengthY >= kLengthEpsilon))
    {
        Setup->AxisInertiaB = 1.0f;
        Setup->InvAxisInertiaB = 1.0f;
    }
    else
    {
        Setup->InvAxisInertiaB = (float)(1.0 / LengthY);
    }

    // Step 7.
    Setup->MassB = CY->Mass;
    Setup->InvMassB = CY->InvMass;
    if ((CX->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0 && (CX->Flags & IVP_CORE_PINNED) == 0)
    {
        Setup->MassA = CX->Mass;
        Setup->InvMassA = CX->InvMass;
    }
    else
    {
        Setup->MassA = Setup->MassB * kTenThousand;
        Setup->InvMassA = Setup->InvMassB * kSmallScale;
        Setup->AxisInertiaA = Setup->AxisInertiaB * kTenThousand;
        Setup->InvAxisInertiaA = Setup->InvAxisInertiaB * kSmallScale;
    }
}

// ---------------------------------------------------------------------------
// FUN_06356000. The sum uses the unrounded second result.
// ---------------------------------------------------------------------------
void IVP_ComputeContactAxisEnergies(IVP_PairContactAxisSetup* Setup)
{
    Setup->SpinEnergy = (float)(IVP_SolveContactAxisEnergySplit(
        Setup->SpinSpeed, Setup->AxisInertiaA, Setup->AxisInertiaB, Setup->InvAxisInertiaA, Setup->InvAxisInertiaB));

    const double Result2 = IVP_SolveContactAxisEnergySplit(
        Setup->LinearSpeed, Setup->MassA, Setup->MassB, Setup->InvMassA, Setup->InvMassB);
    Setup->LinearEnergy = (float)(Result2);

    Setup->EnergySum = (float)(Result2 + Setup->SpinEnergy);
}

// ---------------------------------------------------------------------------
// FUN_06356050, thiscall on the scratch, RET 4. Only CX (+0x60) is gated on
// (Flags & IVP_CORE_PHYSICAL_UNMOVEABLE); the CY block always runs.
// ---------------------------------------------------------------------------
void IVP_ApplyContactAxisImpulses(IVP_PairContactAxisSetup* Setup, float Scale)
{
    // Each root's fourth argument and each result is rounded to float.
    const float Temp1 = (float)(IVP_SolveContactAxisRoot(
        Setup->SpinSpeed, Setup->InvAxisInertiaA, Setup->InvAxisInertiaB, Scale * Setup->SpinEnergy));
    const float Temp2 = (float)(IVP_SolveContactAxisRoot(
        Setup->LinearSpeed, Setup->InvMassA, Setup->InvMassB, Scale * Setup->LinearEnergy));

    IVP_Core* CoreA = Setup->CoreA;
    if ((CoreA->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        // X stays on the x87 stack.
        const double X = (double)(Temp2) * Setup->InvMassA;
        CoreA->SpeedChange.z = (float)(X * Setup->LinearAxis[2] + CoreA->SpeedChange.z);
        CoreA->SpeedChange.x = (float)(X * Setup->LinearAxis[0] + CoreA->SpeedChange.x);
        CoreA->SpeedChange.y = (float)(X * Setup->LinearAxis[1] + CoreA->SpeedChange.y);

        const double Y = (double)(Temp1) * Setup->InvAxisInertiaA;
        CoreA->RotSpeedChange.z = (float)(Y * Setup->SpinAxisA[2] + CoreA->RotSpeedChange.z);
        CoreA->RotSpeedChange.x = (float)(Y * Setup->SpinAxisA[0] + CoreA->RotSpeedChange.x);
        CoreA->RotSpeedChange.y = (float)(Y * Setup->SpinAxisA[1] + CoreA->RotSpeedChange.y);
    }

    // The linear axis is negated in place (DAT_063a5498 = -1.0f).
    Setup->LinearAxis[0] = Setup->LinearAxis[0] * -1.0f;
    Setup->LinearAxis[1] = Setup->LinearAxis[1] * -1.0f;
    Setup->LinearAxis[2] = Setup->LinearAxis[2] * -1.0f;

    IVP_Core* CoreB = Setup->CoreB;
    // Unlike X, X2 is spilled as float.
    const float X2 = Temp2 * Setup->InvMassB;
    CoreB->SpeedChange.z = (float)((double)(Setup->LinearAxis[2]) * X2 + CoreB->SpeedChange.z);
    CoreB->SpeedChange.x = (float)((double)(Setup->LinearAxis[0]) * X2 + CoreB->SpeedChange.x);
    CoreB->SpeedChange.y = (float)((double)(Setup->LinearAxis[1]) * X2 + CoreB->SpeedChange.y);

    const double Y2 = (double)(Temp1) * Setup->InvAxisInertiaB;
    CoreB->RotSpeedChange.z = (float)(Y2 * Setup->SpinAxisB[2] + CoreB->RotSpeedChange.z);
    CoreB->RotSpeedChange.x = (float)(Y2 * Setup->SpinAxisB[0] + CoreB->RotSpeedChange.x);
    CoreB->RotSpeedChange.y = (float)(Y2 * Setup->SpinAxisB[1] + CoreB->RotSpeedChange.y);
}

// ---------------------------------------------------------------------------
// FUN_063561c0
// ---------------------------------------------------------------------------
float IVP_ComputeAndApplyContactAxisImpulse(IVP_ListManager_CorePairEntry* Entry, float Budget)
{
    constexpr float kEnergyEpsilon = 1e-10f; // DAT_063a53d8
    constexpr float kBudgetCapFraction = 0.1f; // DAT_063a5464 (CD CC CC 3D)

    IVP_PairContactAxisSetup Setup{};
    IVP_PopulateContactAxisSetup(&Setup, Entry->CoreA, Entry->CoreB);
    IVP_ComputeContactAxisEnergies(&Setup);

    // Cap stays unrounded; not-greater or unordered keeps the budget.
    const float EnergySum = Setup.EnergySum;
    const double Cap = (double)(EnergySum) * kBudgetCapFraction;
    if (Budget > Cap)
        Budget = (float)(Cap);

    // Less or unordered -> 0.0f.
    if (!(EnergySum >= kEnergyEpsilon))
        return 0.0f; // DAT_063a53d4

    IVP_ApplyContactAxisImpulses(&Setup, Budget / EnergySum);
    return Budget;
}
