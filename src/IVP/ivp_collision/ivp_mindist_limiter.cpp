// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_Ledge_Mindist / IVP_Complex_Mindist (Common.dll 0x06362620-0x063632b0
// plus the base ctor/dtor FUN_0634dd10 / FUN_0634dd60), the IVP_Mindist_Manager
// insert/remove/recalc family, IVP_Ledge_Mindist::RescheduleCollisionEvent (FUN_0634f320),
// the contact-frame dispatcher (FUN_0635a1e0), the pair-record find-or-create
// chain and the IVP_ListManager (friction system) members. See ivp_mindist.h
// for the class layouts.
//
// FUN_06350310 / FUN_06350350 hand a mindist to each side's phantom
// (IVP_Controller_Phantom, object +0x1c; ivp_phantom.cpp).
#include "../ivp_physics/ivp_mindist.h"
#include "../ivp_physics/ivp_environment.h"
#include "../ivp_physics/ivp_ov_element.h" // IVP_Mindist_Manager (IVP_Environment::mindist_manager, +0x10) -- DetachMindist is defined in this file.
#include "../ivp_physics/ivp_material.h"
#include "ivp_listmanager_topology.h" // declarations of the functions defined here (FUN_06355c10, FUN_063562d0)
#include "../ivp_utility/ivp_core_trap.h" // IVP_CoreTrap (retail's `MOV dword ptr [0],0`)
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte (insert_exact_mindist)
#include "../../include/common_physys.h"
#include "ivp_ledge_sweep_solver.h" // the collision-time estimator dispatch layer and its drivers
#include "ivp_mindist_create.h" // IVP_Create_Exact_Mindists (FUN_0634e0b0), called by SpawnOrDeferChild (06362c70)
#include "../ivp_physics/ivp_phantom.h" // IVP_Controller_Phantom (06350310 / 06350350)
#include "../ivp_physics/ivp_contact_point.h" // IVP_Ledge_Mindist::try_to_generate_managed_friction (06356bc0)
#include "ivp_mindist_minimize.h" // IVP_Mindist_Minimize_Solver: 06352cf0 / 06352b90 build it on the stack
#include "ivp_compact_ledge_solver.h" // IVP_Compact_Ledge_Solver: the contact-frame leaves 06359850 / 06359ae0 and 0635a1e0
#include "../ivp_physics/ivp_impact.h" // IVP_Mindist_Do_Impact (FUN_06358680), called by 06358cb0
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <new>
#include <type_traits>


// ---------------------------------------------------------------------------
// Retail's global mindist tolerance block  (.data:0658aa10 .. 0658ab44)
// ---------------------------------------------------------------------------
// One object, G_IVP_MindistSettings (ivp_mindist_settings.h), built at DLL load
// (static initializer .data:063ea60c -> LAB_0634da40: `MOV ECX,0x0658aa10;
// JMP FUN_0634db40`). Consumers' byte offsets map to:
//        DAT_0658aa10 = block[0x00]            (= param*0.1f = 0.001f)
//        DAT_0658aa18 = &block[0x02]           the 64-entry ramp's base, which
//                       is why every user indexes it as `[idx*4 + 0658aa18]`
//        DAT_0658ab24 = block[0x45]            (0x0658ab24-0x0658aa10 = 0x114)
//        DAT_0658ab34 = block[0x49]            (offset 0x124 = block[1]*0.1f)
//        DAT_0658ab3c = block[0x4b]            (offset 0x12c, the int 1000)
//        DAT_0658ab40 = block[0x4c]            (offset 0x130, == 1.0f)
//        DAT_0658ab44 = block[0x4d]            (offset 0x134, an int32 that
//                       FUN_0634db40 does NOT initialize)
#include "../ivp_physics/ivp_mindist_settings.h"

namespace
{
    // DAT_0658aa18[Index] -- retail's unclamped ramp index (<= 0x3f, see
    // IVP_MindistSettingsRamp).
    float IVP_ToleranceRamp(uint32_t Index) { return IVP_MindistSettingsRamp(Index); }
    float IVP_ToleranceBase() { return G_IVP_MindistSettings.real_coll_dist; } // DAT_0658aa10
    float IVP_ToleranceMinRate() { return G_IVP_MindistSettings.speed_after_keeper_dist; } // DAT_0658ab24
    float IVP_ToleranceBias() { return G_IVP_MindistSettings.mindist_change_force_dist; } // DAT_0658ab34
    float IVP_ToleranceEpsScale() { return G_IVP_MindistSettings.EventNudgeEpsScale; } // DAT_0658ab40

    // DAT_0658ab44 (+0x134), only read/written by FUN_0634f320 (:0634f57b /
    // :0634f586 / :0634f5a6) as a plain int counter.
    int32_t& IVP_ToleranceRecomputeCounter()
    {
        return G_IVP_MindistSettings.ToleranceDecayCounter;
    }
}

// FUN_0634f320, defined below next to IVP_Ledge_Mindist::UpdatePerTick.

namespace
{
    // FUN_06350310, cdecl: unless StateFlags & 0xc00 already, set
    // flags = (flags & 0xfffdf7ff) | 0x10400 and hand the mindist to each
    // object's phantom (+0x1c) that exists, object 0 first.
    void IVP_AddMindistToPhantoms(IVP_Ledge_Mindist* M)
    {
        if ((M->StateFlags & 0xc00u) != 0)
            return;
        M->StateFlags = (M->StateFlags & 0xfffdf7ffu) | 0x10400u;
        for (int32_t I = 0; I < 2; ++I)
        {
            IVP_Controller_Phantom* const Phantom = static_cast<IVP_Controller_Phantom*>(M->synapse[I].l_obj->controller_phantom);
            if (Phantom != nullptr)
                Phantom->mindist_entered_volume(M); // FUN_06344400
        }
    }

    // FUN_06350350, cdecl: if StateFlags & 0xc00, clear 0x30c00 and take the
    // mindist out of each object's phantom that exists.
    void IVP_RemoveMindistFromPhantoms(IVP_Ledge_Mindist* M)
    {
        if ((M->StateFlags & 0xc00u) == 0)
            return;
        M->StateFlags &= 0xfffcf3ffu;
        for (int32_t I = 0; I < 2; ++I)
        {
            IVP_Controller_Phantom* const Phantom = static_cast<IVP_Controller_Phantom*>(M->synapse[I].l_obj->controller_phantom);
            if (Phantom != nullptr)
                Phantom->mindist_left_volume(M); // FUN_06344580
        }
    }

    // ---------------------------------------------------------------------
    // The pair-record (IVP_Contact_Point) find-or-create chain that
    // FUN_06356bc0 (try_to_generate_managed_friction) uses: FUN_0635a900,
    // FUN_0635a950, with the match check FUN_06356810 / FUN_06356740.
    // ---------------------------------------------------------------------

    // FUN_06356740: thiscall(this = one record side's node, Record+8 or
    // Record+0x1c; arg = one of the mindist's synapse slots). The node's kind
    // byte (node+0xE, the low byte of Status1/Status2) is read as a SIGNED
    // int8_t and compared with the slot's shape type, then:
    //   0 (Vertex): same owning ledge AND same start point index.
    //   1 (Edge): same edge, or the node's opposite edge is the slot's.
    //   2 (Face): same owning triangle.
    //   3: always matches.
    inline bool IVP_CompareSideFeature(uint16_t RecordSideHash, const void* RecordSideFeature, int16_t SlotShapeType, const void* SlotFeature)
    {
        const int8_t Kind = static_cast<int8_t>(RecordSideHash & 0xFF);
        if (Kind != SlotShapeType)
            return false;

        const IVP_Compact_Edge* NodeEdge = static_cast<const IVP_Compact_Edge*>(RecordSideFeature);
        const IVP_Compact_Edge* TargetEdge = static_cast<const IVP_Compact_Edge*>(SlotFeature);

        switch (Kind)
        {
        case IVP_SYNAPSE_POINT:
            return NodeEdge->GetCompactLedge() == TargetEdge->GetCompactLedge()
                && NodeEdge->GetStartPointIndex() == TargetEdge->GetStartPointIndex();
        case IVP_SYNAPSE_EDGE:
            return NodeEdge == TargetEdge || NodeEdge->GetOppositeEdge() == TargetEdge;
        case IVP_SYNAPSE_POLY:
            return NodeEdge->GetCompactTriangle() == TargetEdge->GetCompactTriangle();
        case IVP_SYNAPSE_BALL:
            return true;
        default:
            // The library's CORE trap (:063567e7), then returns 0.
            IVP_CoreTrap();
            return false;
        }
    }

    // FUN_06356810: thiscall(this = Record, arg = Mindist). Tries BOTH
    // orientations (a record's sides follow the StateFlags bit 0x100 active at
    // construction); object identity and both side features must agree.
    inline bool IVP_ConfirmPairRecordMatch(const IVP_Contact_Point* Record, const IVP_Ledge_Mindist* Mindist)
    {
        const IVP_Synapse_Real& SynA = Mindist->synapse[0];
        const IVP_Synapse_Real& SynB = Mindist->synapse[1];
        if (Record->Object1 == SynA.l_obj && Record->Object2 == SynB.l_obj)
        {
            if (IVP_CompareSideFeature(Record->Status1, Record->Feature1, SynA.status, SynA.edge)
             && IVP_CompareSideFeature(Record->Status2, Record->Feature2, SynB.status, SynB.edge))
                return true;
        }
        if (Record->Object2 == SynA.l_obj && Record->Object1 == SynB.l_obj)
        {
            if (IVP_CompareSideFeature(Record->Status2, Record->Feature2, SynA.status, SynA.edge)
             && IVP_CompareSideFeature(Record->Status1, Record->Feature1, SynB.status, SynB.edge))
                return true;
        }
        return false;
    }

    // DAT_0658ab1c is IVP_Mindist_Settings+0x10c (friction_dist), not a clock.

    // FUN_0635a900. Walks ObjectA->mindist_list (IVP_Real_Object+0x28) for an
    // existing record of this pair. List entries are NODE pointers (record+0x08
    // or record+0x1c), mapped back to the record by the node's self-relative
    // int16 Delta at node+0xC. A candidate with the right object but a failing
    // feature check is skipped and the walk continues (:0635a930).
    inline IVP_Contact_Point* IVP_FindExistingPairRecord(IVP_Ledge_Mindist* Mindist)
    {
        IVP_Real_Object* const ObjB = Mindist->synapse[1].l_obj;
        void* NodePtr = Mindist->synapse[0].l_obj->mindist_list;
        while (NodePtr != nullptr)
        {
            char* NodeBytes = static_cast<char*>(NodePtr);
            int16_t Delta = *reinterpret_cast<int16_t*>(NodeBytes + 0x0C); // Delta1/Delta2, self-relative
            IVP_Contact_Point* Record = reinterpret_cast<IVP_Contact_Point*>(NodeBytes + Delta);
            if ((Record->Object1 == ObjB || Record->Object2 == ObjB)
                && IVP_ConfirmPairRecordMatch(Record, Mindist))
                return Record;
            NodePtr = *reinterpret_cast<void**>(NodeBytes); // Next field, node+0x00
        }
        return nullptr;
    }

    // FUN_063539d0 is IVP_Contact_Point's constructor (ivp_contact_point.cpp).

} // namespace -- IVP_FindOrCreatePairRecord has external linkage

    // FUN_0635a950. *OutTag: 0 = found existing, 1 = newly created.
    IVP_Contact_Point* IVP_FindOrCreatePairRecord(IVP_Ledge_Mindist* Mindist, int32_t* OutTag)
    {
        IVP_Contact_Point* Existing = IVP_FindExistingPairRecord(Mindist);
        if (Existing != nullptr)
        {
            *OutTag = 0;
            return Existing;
        }

        IVP_Contact_Point* Fresh = new IVP_Contact_Point(Mindist); // operator_new(0x74) + FUN_063539d0
        *OutTag = 1;
        return Fresh;
    }

// ---------------------------------------------------------------------
// IVP_ListManager (the friction system; layout in ivp_mindist.h).
// ---------------------------------------------------------------------

// FUN_0634b600, RET 4. Vtables: system 063a54c8 (+0x00), static handle
// 063a5500 (+0x08), energy handle 063a54e4 (+0x10); the handles' back
// pointers (+0x0c, +0x14) = this.
//   `AND [+0x44],0xffffff00` clears only the low byte (union_find_necessary
//     is an 8-bit bit-field, so the port does the same).
//   +0x18..+0x1f, +0x40 and +0x48 are not written.
IVP_ListManager::IVP_ListManager(IVP_Environment* EnvironmentIn)
    : Environment(EnvironmentIn)
    , StaticFsHandle(this)
    , EnergyFsHandle(this)
{
    CtxListHead = nullptr;
    RegisterCoreCallCount = 0;
    CtxListCount = 0;
    union_find_necessary = 0; // retail: low byte only
}

    // FUN_0634b9a0.
    void IVP_ListManager::InsertCtx(IVP_Contact_Point* Node)
    {
        Node->ListManagerOwner = this;
        IVP_Contact_Point* OldHead = CtxListHead;
        Node->ListManagerListPrev = nullptr;
        Node->ListManagerListNext = OldHead;
        CtxListHead = Node;
        if (OldHead != nullptr)
            OldHead->ListManagerListPrev = Node;
        ++CtxListCount;
    }

    // FUN_0634b8d0.
    void IVP_ListManager::RemoveCtx(IVP_Contact_Point* Node)
    {
        IVP_Contact_Point* Prev = Node->ListManagerListPrev;
        IVP_Contact_Point* Next = Node->ListManagerListNext;
        if (Next != nullptr)
            Next->ListManagerListPrev = Prev;
        --CtxListCount;
        if (Prev != nullptr)
            Prev->ListManagerListNext = Next;
        else
            CtxListHead = Next;
    }

    // FUN_06355770. Loose match: each search core must equal either of the
    // entry's cores.
    IVP_ListManager_CorePairEntry* IVP_ListManager::FindCorePairEntry(IVP_Core* CoreA, IVP_Core* CoreB)
    {
        for (int I = CorePairEntries.Len() - 1; I >= 0; --I)
        {
            IVP_ListManager_CorePairEntry* Entry = CorePairEntries[I];
            const bool HitsA = (Entry->CoreA == CoreA || Entry->CoreB == CoreA);
            const bool HitsB = (Entry->CoreA == CoreB || Entry->CoreB == CoreB);
            if (HitsA && HitsB)
                return Entry;
        }
        return nullptr;
    }

    // FUN_06356280: exact match in either order; count is the unsigned word at
    // +0x36, walked from the last element down.
    IVP_ListManager_CorePairEntry* IVP_ListManager::FindCorePairEntryExact(IVP_Core* KeyA, IVP_Core* KeyB)
    {
        for (int I = CorePairEntries.Len() - 1; I >= 0; --I)
        {
            IVP_ListManager_CorePairEntry* Entry = CorePairEntries[I];
            if ((Entry->CoreA == KeyA && Entry->CoreB == KeyB) || (Entry->CoreA == KeyB && Entry->CoreB == KeyA))
                return Entry;
        }
        return nullptr;
    }

    // FUN_0634b900. Retail returns 063556b0's EAX (= Ctx); no caller
    // (063555e5, 06356d33, 06356e84) reads it.
    void IVP_ListManager::FindOrAttachCorePairEntry(IVP_Contact_Point* Ctx)
    {
        IVP_Core* CoreA = Ctx->Object1->core;
        IVP_Core* CoreB = Ctx->Object2->core;

        IVP_ListManager_CorePairEntry* Entry = FindCorePairEntry(CoreA, CoreB);
        if (Entry == nullptr)
        {
            // operator_new(0x30) + FUN_06356240, added via FUN_063556e0.
            Entry = new IVP_ListManager_CorePairEntry();
            Entry->CoreA = CoreA;
            Entry->CoreB = CoreB;
            CorePairEntries.Add(Entry);
        }

        Entry->AttachedCtx.Add(Ctx); // FUN_063556b0
    }

    // FUN_0634b9d0, RET 4. Kinematic cores (Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) are only added to
    // RegisteredCores; RegisterCoreCallCount counts both paths.
    void IVP_ListManager::RegisterCore(IVP_Core* Core)
    {
        RegisteredCores.Add(Core);

        if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
        {
            RegisteredDynamicCores.Add(Core);
            Core->register_controller(&StaticFsHandle);
            Core->register_controller(this);
            Core->register_controller(&EnergyFsHandle);
        }

        ++RegisterCoreCallCount;
    }

    // FUN_0634ba50, RET 4. Retail's removals are inline: when Core is absent
    // it still decrements and shifts from index -1 (see
    // IVP_U_Vector::RemoveElement's divergence note). Controllers are detached
    // in the reverse order of RegisterCore.
    void IVP_ListManager::UnregisterCore(IVP_Core* Core)
    {
        if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
        {
            RegisteredDynamicCores.RemoveElement(Core);
            Core->detach_controller(&EnergyFsHandle);
            Core->detach_controller(this);
            Core->detach_controller(&StaticFsHandle);
        }

        RegisteredCores.RemoveElement(Core);
        --RegisterCoreCallCount;
    }

    // FUN_0634b850. Removes Dist from its core-pair entry's AttachedCtx and,
    // when that was the last one, removes and deletes the entry. Returns 1 if
    // the entry was destroyed (RemoveDistFromSystem, FUN_06355490, tests it;
    // MergeFrom discards it). A missing entry is retail's assert trap
    // (:0634b87a), reproduced.
    int IVP_ListManager::RetirePairRecordBookkeeping(IVP_Contact_Point* Dist)
    {
        IVP_Core* CoreA = Dist->Object1->core;
        IVP_Core* CoreB = Dist->Object2->core;
        IVP_ListManager_CorePairEntry* Entry = FindCorePairEntry(CoreA, CoreB);
        if (Entry == nullptr)
        {
            IVP_CoreTrap();
        }

        Entry->AttachedCtx.RemoveElement(Dist);   // FUN_06355310
        if (Entry->AttachedCtx.n_elems != 0)      // FUN_06355370
            return 0;

        CorePairEntries.RemoveElement(Entry);     // FUN_06355710
        if (Entry != nullptr)
        {
            Entry->AttachedCtx.Clear();           // FUN_063449d0
            delete Entry;                         // FUN_06391d5a
        }
        return 1;
    }

    // The per-core record moved here is IVP_Friction_Info_For_Core
    // (ivp_mindist.h): FrDists at +0x00, the owning system at +0x08.

    // FUN_063555b0's Phase 2. For every Core in Other->RegisteredCores
    // (backward), its hash entries under Other and Target (retail's redundant
    // third lookup at :0635562a is not reproduced; it cannot differ):
    //   - Core already has a Target entry: move every FrDist across one at a
    //     time, backward, and release the empty Other entry. Core's
    //     registrations are left untouched (:06355669 skips them).
    //   - Otherwise: re-key the Other entry to Target in place (unregister,
    //     rewrite, register) and move Core with UnregisterCore/RegisterCore.
    void IVP_MergeRegisteredCores(IVP_ListManager* Target, IVP_ListManager* Other)
    {
        for (int I = static_cast<int>(Other->RegisteredCores.n_elems) - 1; I >= 0; --I)
        {
            IVP_Core* Core = Other->RegisteredCores[I];

            IVP_Friction_Info_For_Core* OtherEntry = static_cast<IVP_Friction_Info_For_Core*>(
                Core->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(Other))));
            IVP_Friction_Info_For_Core* TargetEntry = static_cast<IVP_Friction_Info_For_Core*>(
                Core->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(Target))));

            if (TargetEntry != nullptr)
            {
                for (int J = static_cast<int>(OtherEntry->FrDists.n_elems) - 1; J >= 0; --J)
                {
                    IVP_Contact_Point* Elem = OtherEntry->FrDists[J];
                    TargetEntry->FrDists.Add(Elem);
                    OtherEntry->FrDists.RemoveElement(Elem);
                }
                Core->release_object_hash_entry(OtherEntry);
            }
            else
            {
                Core->unregister_object_hash_entry(OtherEntry);
                OtherEntry->FrictionSystem = Target;
                Core->register_object_hash_entry(OtherEntry);

                Other->UnregisterCore(Core);
                Target->RegisterCore(Core);
            }
        }
    }

    // FUN_063555b0, RET 4.
    void IVP_ListManager::MergeFrom(IVP_ListManager* Other)
    {
        // Phase 1: move every node of Other's Ctx list onto `this`.
        IVP_Contact_Point* Node = Other->CtxListHead;
        while (Node != nullptr)
        {
            IVP_Contact_Point* NextNode = Node->ListManagerListNext;
            Other->RemoveCtx(Node);
            Other->RetirePairRecordBookkeeping(Node);
            this->InsertCtx(Node);
            this->FindOrAttachCorePairEntry(Node);
            Node = NextNode;
        }

        IVP_MergeRegisteredCores(this, Other);

        // Deleting destructor (slot 6), null-tested.
        delete Other;
    }

namespace
{
    // FUN_06356bc0 is IVP_Ledge_Mindist::try_to_generate_managed_friction (ivp_contact_point.cpp).
    // FUN_06358ee0 is in ivp_physics/ivp_impact.cpp; its callees FUN_06358bb0
    // and FUN_06358c50 follow.

    // FUN_06356730: the 7-bit material tag in bits 24-30 of the triangle
    // header of a tagged IVP_Compact_Edge* (Feature1/Feature2).
    inline uint32_t IVP_ReadFeatureTag7(void* TaggedFeature)
    {
        uint32_t* Base = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(TaggedFeature) & ~static_cast<uintptr_t>(0xF));
        return (*Base >> 0x18) & 0x7F;
    }

} // namespace

// FUN_06358bb0, thiscall (this = the contact point), RET 4. Per side: tag 0 ->
// the object's material (+0x8c); otherwise the environment material
// manager's get_material_by_index(nullptr, Tag). The `else` branch always
// uses Object1's environment (EBX is loaded once before the loop).
// Callers: 06353b50, 063562d0, 06358c50, 06359390.
void IVP_Contact_Point::ResolveSideMaterials(IVP_Material** OutArray)
{
    for (int Side = 0; Side < 2; ++Side)
    {
        IVP_Real_Object* SideObject = (Side == 0) ? Object1 : Object2;
        void* SideFeature = (Side == 0) ? Feature1 : Feature2;

        const uint32_t Tag = IVP_ReadFeatureTag7(SideFeature);
        if (Tag == 0)
        {
            OutArray[Side] = SideObject->material;
        }
        else
        {
            OutArray[Side] = Object1->environment->l_material_manager
                ->get_material_by_index(nullptr, static_cast<int>(Tag));
        }
    }
}

// FUN_06358c50, thiscall (this = the contact point), RET 4. The manager pointer
// is read once and used for both dispatches.
void IVP_Contact_Point::PopulateOwner(IVP_Impact_Solver_Long_Term* Owner)
{
    IVP_Real_Object* ObjectA = this->Object1;
    IVP_Real_Object* ObjectB = this->Object2;

    ResolveSideMaterials(Owner->Materials);

    Owner->Objects[0] = ObjectA;
    Owner->Objects[1] = ObjectB;
    Owner->Feature0 = this->Feature1;
    Owner->Feature1 = this->Feature2;

    // Slot 2 (elasticity) into Owner+0x60, then slot 1 (friction) into
    // this+0x44.
    IVP_Material_Manager* Manager = ObjectA->environment->l_material_manager;
    Owner->ContactElasticity = Manager->get_elasticity(Owner);
    this->FrictionCoefficient = Manager->get_friction_factor(Owner);
}

namespace
{

    // FUN_0635a1e0's allocation (:0635a1e9): the temporary contact frame comes
    // from the arena at environment+0xa4 (FUN_06347380, 0xd8 bytes), which the
    // callers reset after the impact pass. The reused block is not cleared:
    // only Status (+0x52) = 0 and the dword at +0x54 &= 0xfffffc00; then Owner
    // is published and environment+0x80 incremented.
    inline IVP_Impact_Solver_Long_Term* IVP_AllocatePairRecordOwner(IVP_Contact_Point* Candidate)
    {
        static_assert(std::is_trivially_default_constructible<IVP_Impact_Solver_Long_Term>::value &&
                      std::is_trivially_destructible<IVP_Impact_Solver_Long_Term>::value,
                      "Contact frames must not construct or retain owned resources in the scratch arena");
        // The modern alignas(16) object has tail padding; both sizeof(T) and
        // retail's 0xd8 request consume the same 0xe0-byte arena slot.
        static_assert(((sizeof(IVP_Impact_Solver_Long_Term) + 0x1fu) & ~size_t(0x1f)) == 0xe0u,
                      "Contact frame must retain the listing's rounded arena stride");
        IVP_Environment* const Environment = Candidate->Object1->environment;
        void* const Storage = Environment->sim_unit_mem->GetMem(
            static_cast<int>(sizeof(IVP_Impact_Solver_Long_Term)));
        IVP_Impact_Solver_Long_Term* const Owner = ::new (Storage) IVP_Impact_Solver_Long_Term;
        uint32_t PackedState;
        std::memcpy(&PackedState, &Owner->StateProcessed, sizeof(PackedState));
        Owner->Status = 0;
        PackedState &= 0xfffffc00u;
        std::memcpy(&Owner->StateProcessed, &PackedState, sizeof(PackedState));
        Candidate->Owner = Owner;
        ++Environment->long_term_solver_count;
        return Owner;
    }

    // Offsets this cluster's three contact-frame builders reach through raw
    // casts or read as a wider type than the header declares. Pinned here
    // rather than in ivp_mindist.h because this file is the only consumer.
    static_assert(offsetof(IVP_Contact_Point, InvFaceNormalLength) == 0x4C, "IVP_Contact_Point+0x4C");
    static_assert(offsetof(IVP_Contact_Point, GapDistance) == 0x58, "IVP_Contact_Point+0x58");
    static_assert(offsetof(IVP_Contact_Point, RecheckFeatureFlags) == 0x60, "IVP_Contact_Point+0x60");
    static_assert(offsetof(IVP_Impact_Solver_Long_Term, ContactNormalWS) == 0x00, "IVP_Impact_Solver_Long_Term+0x00");
    static_assert(offsetof(IVP_Impact_Solver_Long_Term, StateProcessed) == 0x54, "IVP_Impact_Solver_Long_Term+0x54");

    // :063598cc and :06359ceb: a FULL DWORD read/modify/write at the offset
    // ivp_mindist.h declares as the byte `StateProcessed` (06358ee0 reads the
    // same packed dword). Bit 0x200 cleared, 0x100 set when the closest point
    // falls OUTSIDE the matched feature (outside the triangle, or outside the
    // segment's [0,1] range).
    inline void IVP_FlagOwnerContactOutsideFeature(IVP_Impact_Solver_Long_Term* Owner)
    {
        uint32_t* const State = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(Owner) + 0x54);
        *State = (*State & 0xFFFFFDFFu) | 0x00000100u;
    }

    // The owner's ContactNormalWS (+0x00) and TangentAxis0 (+0x78) are 16-byte
    // slots that retail hands to IVP_U_Float_Point methods (FUN_06347ef0 with
    // ECX = owner / owner+0x78); the methods touch only k[0..2].
    inline IVP_U_Point* IVP_ContactFrameSlotAsPoint(Vector* Slot)
    {
        return reinterpret_cast<IVP_U_Point*>(Slot);
    }

    uint32_t IVP_ContactFrameFloatBits(float Value)
    {
        uint32_t Bits;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    // ---------------------------------------------------------------------------
    // FUN_063599f0 (thiscall on the record, RET 0x10): the point vs point
    // leaf of 0635a1e0 (second switch cases 0 and 3); Situation is never read.
    // The tangent reference axis is chosen by x*x >= 0.9f (less or unordered
    // picks the other one).
    // ---------------------------------------------------------------------------
    inline void IVP_BuildPointPointContactFrame(IVP_Contact_Point* Candidate, const IVP_U_Point* PointA,
                                                const IVP_U_Point* PointB, IVP_Impact_Solver_Long_Term* Owner, Vector* /*Situation*/)
    {
        IVP_U_Point Delta;
        Delta.x = (float)((double)(PointB->x) - PointA->x);
        Delta.y = (float)((double)(PointB->y) - PointA->y);
        Delta.z = (float)((double)(PointB->z) - PointA->z);
        Candidate->GapDistance = (float)(Delta.real_length_plus_normize()); // FUN_06347e90

        Owner->ContactNormalWS.x = Delta.x;
        Owner->ContactNormalWS.y = Delta.y;
        Owner->ContactNormalWS.z = Delta.z;

        double ReferenceR;
        float ReferenceQ;
        if ((double)(Delta.x) * Delta.x >= (double)(0.9f)) // DAT_063a53fc
        {
            ReferenceR = 0.0; // DAT_063a53d4
            ReferenceQ = 1.0f;
        }
        else
        {
            ReferenceR = 1.0; // DAT_063a53d0
            ReferenceQ = 0.0f;
        }

        const float YTimesR = (float)((double)(Delta.y) * ReferenceR); // spilled to param_3
        Owner->TangentAxis0.x = (float)((double)(Delta.y) * ReferenceQ);
        Owner->TangentAxis0.y = (float)((double)(Delta.z) * ReferenceR - (double)(Delta.x) * ReferenceQ);
        Owner->TangentAxis0.z = (float)(0.0 - (double)(YTimesR));
        IVP_ContactFrameSlotAsPoint(&Owner->TangentAxis0)->normize(); // FUN_06347ef0

        Owner->SolverContactPointWS.x = PointA->x;
        Owner->SolverContactPointWS.y = PointA->y;
        Owner->SolverContactPointWS.z = PointA->z;
    }

    // ---------------------------------------------------------------------------
    // FUN_06359850 (thiscall on the record, RET 0x14): the point vs triangle
    // leaf (second switch case 2); Situation is never read. When record+0x60's
    // low byte is 1 it is cleared and a point outside the triangle (any QR
    // sign bit) flags the owner. The normal is scaled by record+0x4c and
    // negated (DAT_063a5498) for ContactNormalWS; the tangent is F's edge.
    // ---------------------------------------------------------------------------
    inline void IVP_BuildPointPolyContactFrame(IVP_Contact_Point* Candidate, const IVP_U_Point* Point,
                                               const IVP_Compact_Edge* F, const IVP_Cache_Ledge_Point* CacheF,
                                               IVP_Impact_Solver_Long_Term* Owner, Vector* /*Situation*/)
    {
        Owner->SolverContactPointWS.x = Point->x;
        Owner->SolverContactPointWS.y = Point->y;
        Owner->SolverContactPointWS.z = Point->z;

        IVP_U_Point LocalPoint;
        CacheF->cache_object->transform_world_to_slot_space(Point, &LocalPoint); // FUN_0634d860

        if (static_cast<uint8_t>(Candidate->RecheckFeatureFlags) == 1)
        {
            Candidate->RecheckFeatureFlags &= 0xFFFFFF00u;
            IVP_Unscaled_QR_Result Qr;
            IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheF->compact_ledge, F, &LocalPoint, &Qr);
            const uint32_t SignBits = (IVP_ContactFrameFloatBits(Qr.Checks[1]) | IVP_ContactFrameFloatBits(Qr.Checks[2])) |
                                      IVP_ContactFrameFloatBits(Qr.Checks[0]);
            if ((SignBits & 0x80000000u) != 0u)
                IVP_FlagOwnerContactOutsideFeature(Owner);
        }

        IVP_U_Point Normal;
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(F, CacheF->compact_ledge, &Normal);
        const double InvNormalLength = Candidate->InvFaceNormalLength;
        Normal.y = (float)((double)(Normal.y) * InvNormalLength);
        Normal.z = (float)((double)(Normal.z) * InvNormalLength);
        Normal.x = (float)((double)(Normal.x) * InvNormalLength);

        IVP_U_Point WorldNormal;
        CacheF->cache_object->rotate_slot_to_world_space(&Normal, &WorldNormal); // FUN_0634d9d0

        const IVP_U_Float_Point* const V0 = CacheF->compact_poly_points + F->GetStartPointIndex();
        const IVP_U_Float_Point* const V1 = CacheF->compact_poly_points + F->GetNextEdge()->GetStartPointIndex();
        const double PointDot = ((double)(Normal.z) * LocalPoint.z + (double)(Normal.y) * LocalPoint.y) +
                                (double)(LocalPoint.x) * Normal.x;
        const double VertexDot = ((double)(Normal.z) * V0->Z + (double)(Normal.y) * V0->Y) +
                                 (double)(Normal.x) * V0->X;
        Candidate->GapDistance = (float)(PointDot - VertexDot);

        Owner->ContactNormalWS.x = (float)((double)(WorldNormal.x) * -1.0f);
        Owner->ContactNormalWS.y = (float)((double)(WorldNormal.y) * -1.0f);
        Owner->ContactNormalWS.z = (float)((double)(WorldNormal.z) * -1.0f);

        IVP_U_Point EdgeVec;
        EdgeVec.x = (float)((double)(V1->X) - V0->X);
        EdgeVec.y = (float)((double)(V1->Y) - V0->Y);
        EdgeVec.z = (float)((double)(V1->Z) - V0->Z);
        IVP_U_Point Tangent;
        CacheF->cache_object->rotate_slot_to_world_space(&EdgeVec, &Tangent); // retail writes Owner+0x78 directly
        Owner->TangentAxis0.x = Tangent.x;
        Owner->TangentAxis0.y = Tangent.y;
        Owner->TangentAxis0.z = Tangent.z;
    }

    // ---------------------------------------------------------------------------
    // FUN_06359ae0 (thiscall on the record, RET 0x14): the point vs edge leaf
    // (second switch case 1); Situation is never read. p = t x e scaled to a
    // unit vector (or (1, 0, 0) when d*d <= 1e-10f or unordered); the normal is
    // p x e. When record+0x60's low byte is 1 it is cleared and a projection
    // parameter outside [0, 1] (or unordered) flags the owner.
    // ---------------------------------------------------------------------------
    inline void IVP_BuildPointEdgeContactFrame(IVP_Contact_Point* Candidate, const IVP_U_Point* Point,
                                               const IVP_Compact_Edge* K, const IVP_Cache_Ledge_Point* CacheK,
                                               IVP_Impact_Solver_Long_Term* Owner, Vector* /*Situation*/)
    {
        IVP_U_Point V0;
        IVP_U_Point V1;
        IVP_Compact_Ledge_Solver::give_world_coords_AT(K, CacheK, &V0);
        IVP_Compact_Ledge_Solver::give_world_coords_AT(K->GetNextEdge(), CacheK, &V1);

        Vector EdgeDir;
        EdgeDir.x = (float)((double)(V1.x) - V0.x);
        EdgeDir.y = (float)((double)(V1.y) - V0.y);
        EdgeDir.z = (float)((double)(V1.z) - V0.z);
        Vector ToPoint;
        ToPoint.x = (float)((double)(Point->x) - V0.x);
        ToPoint.y = (float)((double)(Point->y) - V0.y);
        ToPoint.z = (float)((double)(Point->z) - V0.z);

        const float EdgeQuadLength = (float)(
            ((double)(EdgeDir.z) * EdgeDir.z + (double)(EdgeDir.y) * EdgeDir.y) +
            (double)(EdgeDir.x) * EdgeDir.x);
        const float InvEdgeLength = (float)(1.0 / std::sqrt((double)(EdgeQuadLength))); // FUN_06347df0

        IVP_U_Point Perp;
        Perp.x = (float)((double)(ToPoint.z) * EdgeDir.y - (double)(EdgeDir.z) * ToPoint.y);
        Perp.y = (float)((double)(EdgeDir.z) * ToPoint.x - (double)(ToPoint.z) * EdgeDir.x);
        Perp.z = (float)((double)(ToPoint.y) * EdgeDir.x - (double)(EdgeDir.y) * ToPoint.x);

        const double Distance = Perp.real_length() * InvEdgeLength; // FUN_06347e60, kept unrounded
        Candidate->GapDistance = (float)(Distance);
        if (Distance * Distance > (double)(1.0e-10f))
        {
            const double Scale = InvEdgeLength / Distance;
            Perp.z = (float)((double)(Perp.z) * Scale);
            Perp.x = (float)((double)(Perp.x) * Scale);
            Perp.y = (float)((double)(Perp.y) * Scale);
        }
        else
        {
            Perp.x = 1.0f;
            Perp.y = 0.0f;
            Perp.z = 0.0f;
        }

        Owner->ContactNormalWS.x = (float)((double)(Perp.z) * EdgeDir.y - (double)(Perp.y) * EdgeDir.z);
        Owner->ContactNormalWS.y = (float)((double)(EdgeDir.z) * Perp.x - (double)(Perp.z) * EdgeDir.x);
        Owner->ContactNormalWS.z = (float)((double)(Perp.y) * EdgeDir.x - (double)(EdgeDir.y) * Perp.x);
        IVP_ContactFrameSlotAsPoint(&Owner->ContactNormalWS)->normize(); // FUN_06347ef0

        Owner->SolverContactPointWS.x = Point->x;
        Owner->SolverContactPointWS.y = Point->y;
        Owner->SolverContactPointWS.z = Point->z;
        Owner->TangentAxis0.x = Perp.x;
        Owner->TangentAxis0.y = Perp.y;
        Owner->TangentAxis0.z = Perp.z;

        if (static_cast<uint8_t>(Candidate->RecheckFeatureFlags) == 1)
        {
            Candidate->RecheckFeatureFlags &= 0xFFFFFF00u;
            // FUN_0628be40 (this = &EdgeDir, arg = &ToPoint): (t.z*e.z + t.y*e.y) + t.x*e.x.
            const double Dot = ((double)(ToPoint.z) * EdgeDir.z + (double)(ToPoint.y) * EdgeDir.y) +
                               (double)(ToPoint.x) * EdgeDir.x;
            const double Along = Dot * ((double)(InvEdgeLength) * InvEdgeLength);
            if (!(Along >= 0.0) || Along > 1.0)
                IVP_FlagOwnerContactOutsideFeature(Owner);
        }
    }

    // FUN_06359d10, the edge vs edge leaf (first switch's case 1), is
    // IVP_Contact_Point::CalcFrictionEdgeEdge in ivp_contact_point.cpp.

} // namespace -- IVP_Contact_Point::UpdateContactFrame has external linkage
  // (ivp_friction_solver.cpp calls it too).

// ---------------------------------------------------------------------------
// FUN_0635a1e0 (thiscall on the pair record, no arguments): builds the
// record's contact frame.
//   * The owner comes from the environment's short-term arena (see
//     IVP_AllocatePairRecordOwner); the caller's FUN_06344070 reset reclaims it.
//   * Shape tags are the sign-extended low bytes of record+0x16 / +0x2a; side
//     2's cache is built before side 1's.
//   * First switch on tag1: 0 vertex, 1 edge-edge (FUN_06359d10, then the
//     tail), 3 ball (A = cache origin, record+0x60 low byte = 1), else CORE
//     trap (:0635a413) and the tail. Second switch on tag2 (unsigned) picks
//     the point-point / point-edge / point-triangle leaf.
//   * Tail: contact point pushed out by object1's radius (+0x94); the gap
//     loses both radii and is clamped at 0 (less or unordered); both cache
//     refcounts dropped; TangentAxis1 = n x t0; per side (kinematic cores
//     zeroed) the object-space lever arm, its cross with the normal, the point
//     speed and the effective-mass term; then the friction displacement
//     (rec+0x38 / +0x3c) is advanced by the tangential relative speed times
//     the time since the record's stamp, and the stamp is renewed.
// ---------------------------------------------------------------------------
void IVP_Contact_Point::UpdateContactFrame()
{
    IVP_Impact_Solver_Long_Term* const NewOwner = IVP_AllocatePairRecordOwner(this);

    const int32_t ShapeTag1 = static_cast<int8_t>(static_cast<uint8_t>(Status1 & 0xFFu));
    const int32_t ShapeTag2 = static_cast<int8_t>(static_cast<uint8_t>(Status2 & 0xFFu));
    Vector Situation = { 0.0f, 0.0f, 0.0f };

    IVP_Compact_Edge* const Edge2 = Feature2;
    IVP_Cache_Ledge_Point Cache2;
    Cache2.compact_ledge = Edge2->GetCompactLedge();
    Cache2.cache_object = Object2->ResolveNarrowPhaseCacheSlot(); // FUN_06353510
    Cache2.object = Object2;
    Cache2.compact_poly_points = Cache2.compact_ledge->GetPointArray();
    // The library's friction synapse (record+0x1c); none of the leaves reads it.
    Cache2.synapse = reinterpret_cast<IVP_Synapse_Real*>(&Next2);

    IVP_Compact_Edge* const Edge1 = Feature1;
    IVP_Cache_Ledge_Point Cache1;
    Cache1.compact_ledge = Edge1->GetCompactLedge();
    Cache1.cache_object = Object1->ResolveNarrowPhaseCacheSlot();
    Cache1.object = Object1;
    Cache1.compact_poly_points = Cache1.compact_ledge->GetPointArray();
    Cache1.synapse = reinterpret_cast<IVP_Synapse_Real*>(&Next1); // record+0x8

    IVP_U_Point PointA; // local_38
    IVP_U_Point PointB; // local_68
    bool bSecondSwitch = true;
    switch (ShapeTag1)
    {
    case IVP_SYNAPSE_POINT:
        IVP_Compact_Ledge_Solver::give_world_coords_AT(Edge1, &Cache1, &PointA);
        break;
    case IVP_SYNAPSE_EDGE:
        CalcFrictionEdgeEdge(Edge1, Edge2, &Cache1, &Cache2, NewOwner, &Situation);
        bSecondSwitch = false;
        break;
    case IVP_SYNAPSE_BALL:
        PointA.x = Cache1.cache_object->WorldTransform.vv.x;
        PointA.y = Cache1.cache_object->WorldTransform.vv.y;
        PointA.z = Cache1.cache_object->WorldTransform.vv.z;
        RecheckFeatureFlags = (RecheckFeatureFlags & 0xFFFFFF01u) | 0x1u;
        break;
    default:
        // The library's CORE trap; retail then runs the tail.
        IVP_CoreTrap();
        bSecondSwitch = false;
        break;
    }

    if (bSecondSwitch)
    {
        switch (static_cast<uint32_t>(ShapeTag2))
        {
        case IVP_SYNAPSE_POINT:
            IVP_Compact_Ledge_Solver::give_world_coords_AT(Edge2, &Cache2, &PointB);
            IVP_BuildPointPointContactFrame(this, &PointA, &PointB, NewOwner, &Situation);
            break;
        case IVP_SYNAPSE_EDGE:
            IVP_BuildPointEdgeContactFrame(this, &PointA, Edge2, &Cache2, NewOwner, &Situation);
            break;
        case IVP_SYNAPSE_POLY:
            IVP_BuildPointPolyContactFrame(this, &PointA, Edge2, &Cache2, NewOwner, &Situation);
            break;
        case IVP_SYNAPSE_BALL:
            IVP_BuildPointPointContactFrame(this, &PointA, &Cache2.cache_object->WorldTransform.vv, NewOwner, &Situation);
            break;
        default:
            IVP_CoreTrap();
            break;
        }
    }

    // FUN_0628be60(dst = base = &SolverContactPointWS, dir = &ContactNormalWS, r1).
    const float Radius1 = Object1->extra_radius;
    NewOwner->SolverContactPointWS.y = (float)((double)(Radius1) * NewOwner->ContactNormalWS.y + NewOwner->SolverContactPointWS.y);
    NewOwner->SolverContactPointWS.z = (float)((double)(Radius1) * NewOwner->ContactNormalWS.z + NewOwner->SolverContactPointWS.z);
    NewOwner->SolverContactPointWS.x = (float)((double)(Radius1) * NewOwner->ContactNormalWS.x + NewOwner->SolverContactPointWS.x);

    const double SurfaceGap = (double)(GapDistance) -
                              ((double)(Object2->extra_radius) + Radius1);
    GapDistance = (float)(SurfaceGap);
    if (!(SurfaceGap >= 0.0))
        GapDistance = 0.0f;

    --Cache1.cache_object->RefCount;
    --Cache2.cache_object->RefCount;

    IVP_ContactFrameSlotAsPoint(&NewOwner->TangentAxis0)->normize(); // FUN_06347ef0

    // TangentAxis1 = n x t0.
    const Vector& Normal = NewOwner->ContactNormalWS;
    const Vector& Tangent0 = NewOwner->TangentAxis0;
    NewOwner->TangentAxis1.x = (float)((double)(Normal.y) * Tangent0.z - (double)(Tangent0.y) * Normal.z);
    NewOwner->TangentAxis1.y = (float)((double)(Normal.z) * Tangent0.x - (double)(Normal.x) * Tangent0.z);
    NewOwner->TangentAxis1.z = (float)((double)(Tangent0.y) * Normal.x - (double)(Normal.y) * Tangent0.x);

    Vector RelativeSpeed; // local_58

    // ---- side 1, object1's core ----
    IVP_Core* const Core0 = Object1->core;
    if ((*reinterpret_cast<const uint8_t*>(Core0) & 0x0Cu) != 0)
    {
        RelativeSpeed.x = 0.0f;
        RelativeSpeed.y = 0.0f;
        RelativeSpeed.z = 0.0f;
        NewOwner->EffectiveMassDenom = 0.0f;
        NewOwner->CoordOS[0].x = 0.0f;
        NewOwner->CoordOS[0].y = 0.0f;
        NewOwner->CoordOS[0].z = 0.0f;
        NewOwner->CoordOSCrossNormal[0].x = 0.0f;
        NewOwner->CoordOSCrossNormal[0].y = 0.0f;
        NewOwner->CoordOSCrossNormal[0].z = 0.0f;
        NewOwner->ContactCores[0] = nullptr;
    }
    else
    {
        const IVP_U_Matrix& M = Core0->MWorldFCore;
        const double Dx = (double)(NewOwner->SolverContactPointWS.x) - M.vv.x;
        const double Dy = (double)(NewOwner->SolverContactPointWS.y) - M.vv.y;
        const double Dz = (double)(NewOwner->SolverContactPointWS.z) - M.vv.z;
        Vector& Coord = NewOwner->CoordOS[0];
        Coord.x = (float)((M.rows[1].x * Dy + M.rows[2].x * Dz) + M.rows[0].x * Dx);
        Coord.y = (float)((M.rows[1].y * Dy + M.rows[2].y * Dz) + M.rows[0].y * Dx);
        Coord.z = (float)((M.rows[1].z * Dy + M.rows[2].z * Dz) + M.rows[0].z * Dx);

        // The normal in object space; x stays on the stack.
        const double NormalOSx = ((double)(M.rows[1].x) * Normal.y + (double)(M.rows[2].x) * Normal.z) +
                                 (double)(M.rows[0].x) * Normal.x;
        const float NormalOSy = (float)(((double)(M.rows[2].y) * Normal.z + (double)(M.rows[0].y) * Normal.x) +
                                                   (double)(M.rows[1].y) * Normal.y);
        const float NormalOSz = (float)(((double)(M.rows[1].z) * Normal.y + (double)(M.rows[2].z) * Normal.z) +
                                                   (double)(M.rows[0].z) * Normal.x);

        const float CrossZa = (float)((double)(NormalOSy) * Coord.x);
        const float CrossZb = (float)(NormalOSx * Coord.y);
        NewOwner->CoordOSCrossNormal[0].x = (float)((double)(NormalOSz) * Coord.y - (double)(NormalOSy) * Coord.z);
        NewOwner->CoordOSCrossNormal[0].y = (float)(NormalOSx * Coord.z - (double)(NormalOSz) * Coord.x);
        NewOwner->CoordOSCrossNormal[0].z = (float)((double)(CrossZa) - CrossZb);

        Core0->calc_point_speed_ws(&NewOwner->CoordOS[0], &Core0->Speed, &Core0->RotSpeed,
                                   &RelativeSpeed); // FUN_063461a0

        const Vector& Cross = NewOwner->CoordOSCrossNormal[0];
        NewOwner->EffectiveMassDenom = (float)(
            (((double)(Cross.z) * Core0->InvRotInertia.z * Cross.z +
              (double)(Cross.y) * Core0->InvRotInertia.y * Cross.y) +
             (double)(Cross.x) * Core0->InvRotInertia.x * Cross.x) +
            Core0->InvMass);
        NewOwner->ContactCores[0] = Core0;
    }

    // ---- side 2, object2's core: the same with its own operand order ----
    IVP_Core* const Core1 = Object2->core;
    if ((*reinterpret_cast<const uint8_t*>(Core1) & 0x0Cu) != 0)
    {
        NewOwner->CoordOS[1].x = 0.0f;
        NewOwner->CoordOS[1].y = 0.0f;
        NewOwner->CoordOS[1].z = 0.0f;
        NewOwner->CoordOSCrossNormal[1].x = 0.0f;
        NewOwner->CoordOSCrossNormal[1].y = 0.0f;
        NewOwner->CoordOSCrossNormal[1].z = 0.0f;
        NewOwner->ContactCores[1] = nullptr;
    }
    else
    {
        const IVP_U_Matrix& M = Core1->MWorldFCore;
        const double Dx = (double)(NewOwner->SolverContactPointWS.x) - M.vv.x;
        const double Dy = (double)(NewOwner->SolverContactPointWS.y) - M.vv.y;
        const double Dz = (double)(NewOwner->SolverContactPointWS.z) - M.vv.z;
        Vector& Coord = NewOwner->CoordOS[1];
        Coord.x = (float)((M.rows[2].x * Dz + M.rows[1].x * Dy) + M.rows[0].x * Dx);
        Coord.y = (float)((M.rows[0].y * Dx + M.rows[1].y * Dy) + M.rows[2].y * Dz);
        Coord.z = (float)((M.rows[2].z * Dz + M.rows[0].z * Dx) + M.rows[1].z * Dy);

        const double NormalOSx = ((double)(M.rows[2].x) * Normal.z + (double)(M.rows[0].x) * Normal.x) +
                                 (double)(M.rows[1].x) * Normal.y;
        const float NormalOSy = (float)(((double)(M.rows[1].y) * Normal.y + (double)(M.rows[2].y) * Normal.z) +
                                                   (double)(M.rows[0].y) * Normal.x);
        const float NormalOSz = (float)(((double)(M.rows[2].z) * Normal.z + (double)(M.rows[0].z) * Normal.x) +
                                                   (double)(M.rows[1].z) * Normal.y);

        const float CrossZa = (float)((double)(NormalOSy) * Coord.x);
        const float CrossZb = (float)(NormalOSx * Coord.y);
        NewOwner->CoordOSCrossNormal[1].x = (float)((double)(NormalOSz) * Coord.y - (double)(NormalOSy) * Coord.z);
        NewOwner->CoordOSCrossNormal[1].y = (float)(NormalOSx * Coord.z - (double)(NormalOSz) * Coord.x);
        NewOwner->CoordOSCrossNormal[1].z = (float)((double)(CrossZa) - CrossZb);

        Vector PointSpeed1; // local_38
        Core1->calc_point_speed_ws(&NewOwner->CoordOS[1], &Core1->Speed, &Core1->RotSpeed,
                                   &PointSpeed1); // FUN_063461a0
        // FUN_0628bea0: RelativeSpeed -= PointSpeed1.
        RelativeSpeed.y = (float)((double)(RelativeSpeed.y) - PointSpeed1.y);
        RelativeSpeed.z = (float)((double)(RelativeSpeed.z) - PointSpeed1.z);
        RelativeSpeed.x = (float)((double)(RelativeSpeed.x) - PointSpeed1.x);

        const Vector& Cross = NewOwner->CoordOSCrossNormal[1];
        NewOwner->EffectiveMassDenom = (float)(
            ((((double)(Core1->InvRotInertia.z) * Cross.z * Cross.z +
               (double)(Cross.y) * Core1->InvRotInertia.y * Cross.y) +
              (double)(Core1->InvRotInertia.x) * Cross.x * Cross.x) +
             Core1->InvMass) +
            NewOwner->EffectiveMassDenom);
        NewOwner->ContactCores[1] = Core1;
    }

    NewOwner->EffectiveMassDenomRecip = (float)(1.0 / (double)(NewOwner->EffectiveMassDenom)); // DAT_063a53d0 = 1.0f

    IVP_Environment* const Env = Object1->environment;
    const float NowBase = Env->current_time.time_base;
    const float NowOffset = Env->current_time.time_offset;
    const double DeltaTime = (((double)(NowBase) - LastRecalcTimeBase) + NowOffset) - LastRecalcTimeOffset;
    LastRecalcTimeBase = NowBase;
    LastRecalcTimeOffset = NowOffset;

    const double Slip0 = ((double)(RelativeSpeed.z) * NewOwner->TangentAxis0.z + (double)(RelativeSpeed.y) * NewOwner->TangentAxis0.y) +
                         (double)(RelativeSpeed.x) * NewOwner->TangentAxis0.x;
    SpanFrictionS[0] = (float)((double)(SpanFrictionS[0]) - Slip0 * DeltaTime);
    const double Slip1 = ((double)(RelativeSpeed.z) * NewOwner->TangentAxis1.z + (double)(RelativeSpeed.y) * NewOwner->TangentAxis1.y) +
                         (double)(RelativeSpeed.x) * NewOwner->TangentAxis1.x;
    SpanFrictionS[1] = (float)((double)(SpanFrictionS[1]) - Slip1 * DeltaTime);
}


// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::do_impact -- FUN_06358cb0, vtable 063a55e0 slot 8 (the
// library's IVP_Mindist::do_impact). Revives both objects (FUN_063450e0),
// brackets the impact (FUN_06358680) with the short-term arena's State word
// (env+0xa4) and its reset (FUN_06344070), and recalcs the interpolated frame
// (FUN_06347240) of each moving, non-0x300 core. The movement test is retail's
// `SHL 24 / CMP 0x8000000` on the signed low byte.
// ---------------------------------------------------------------------------
void IVP_Ledge_Mindist::do_impact()
{
    IVP_Real_Object* const ObjA = synapse[0].l_obj;
    IVP_Real_Object* const ObjB = synapse[1].l_obj;
    IVP_Environment* const Env = ObjA->environment;

    ObjA->ReviveFrictionCoreIfSleeping();
    ObjB->ReviveFrictionCoreIfSleeping();

    ++Env->sim_unit_mem->State;

    if (int8_t(ObjA->core->MovementState & 0xff) < IVP_MT_NOT_SIM && (ObjA->core->Flags & IVP_CORE_PINNED) == 0)
        ObjA->core->recalc_interpolated_frame();
    if (int8_t(ObjB->core->MovementState & 0xff) < IVP_MT_NOT_SIM && (ObjB->core->Flags & IVP_CORE_PINNED) == 0)
        ObjB->core->recalc_interpolated_frame();

    ++Env->mindist_event_stamp;

    IVP_Mindist_Do_Impact(this, ObjA, ObjB);

    --Env->sim_unit_mem->State;
    Env->sim_unit_mem->reset_chunks(); // FUN_06344070
}

namespace
{

    // The ledgetree-node radius (node+0x14) of a ledge, as FUN_06362840 /
    // LAB_06362920 read it: `*(float*)((char*)Ledge + Ledge->LedgetreeNodeOffset + 0x14)`.
    inline float LedgetreeNodeRadius(const IVP_Compact_Ledge* Ledge)
    {
        const char* Base = reinterpret_cast<const char*>(Ledge);
        return *reinterpret_cast<const float*>(Base + Ledge->LedgetreeNodeOffset + 0x14);
    }

    // The "does FeatureA or FeatureB win" decision shared by FUN_06362840 and
    // LAB_06362920: A flagged (SizeAndFlags & 3) and B not -> B (0); both
    // flagged -> the larger ledgetree-node radius (ties to A); else A (1).
    int32_t RecalcWinnerFlag(const IVP_Compact_Edge* FeatureA, const IVP_Compact_Edge* FeatureB)
    {
        const IVP_Compact_Ledge* LedgeA = FeatureA->GetCompactLedge();
        const IVP_Compact_Ledge* LedgeB = FeatureB->GetCompactLedge();
        if ((LedgeA->SizeAndFlags & IVP_Compact_Ledge::kHasChildrenMask) != 0)
        {
            if ((LedgeB->SizeAndFlags & IVP_Compact_Ledge::kHasChildrenMask) == 0) return 0;
            if (LedgetreeNodeRadius(LedgeB) < LedgetreeNodeRadius(LedgeA)) return 0;
        }
        return 1;
    }

    // Used by IVP_Complex_Mindist::do_impact (LAB_06362650): bit 31 of the
    // OWNING TRIANGLE's TriInfo (retail masks the edge pointer, :0636278c) --
    // a different bit from IVP_Compact_Edge::IsPiercing() (the edge's own
    // word); do_impact uses both in different branches.
    inline bool TriangleFlagBit31Set(const IVP_Compact_Edge* Feature)
    {
        return (Feature->GetCompactTriangle()->TriInfo & 0x80000000u) != 0;
    }
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::DispatchNarrowPhaseSolve -- FUN_06352cf0
// ---------------------------------------------------------------------------
// Thiscall, no arguments. Once per sim step (env+0x110 vs this+0x70), runs the
// 0x81c-byte IVP_Mindist_Minimize_Solver (P_Finish_Counter = 0x14) through
// the 16-entry table DAT_0658ab4c, indexed by the sorted synapses' shape
// statuses.
//   result OK          -> StateFlags &= 0xffff3fff, return (no rescue push).
//   otherwise           StateFlags = (StateFlags & 0xffff7fff) | 0x4000;
//   ENDLESS_LOOP        -> tail;
//   BACKSIDE            -> proove_polypoly (06352b30), up to two retries;
//   anything else       -> CORE trap, dispatch again (no retry consumed).
// Tail: slot 5 (mindist_rescue_push) unless (StateFlags & 0x3000) == 0x1000
// or (StateFlags & 0x3c0000) == 0x100000.
int32_t IVP_Ledge_Mindist::DispatchNarrowPhaseSolve()
{
    const uint32_t Tick = static_cast<uint32_t>(synapse[0].l_obj->environment->sim_step_count);
    if (LastDispatchTick == Tick)
        return IVP_MRC_ALREADY_CALCULATED;
    LastDispatchTick = Tick;

    IVP_Mindist_Minimize_Solver Solver; // trivial destructor (unwind -> empty RET 062a8920)
    Solver.mindist = this;
    Solver.loop_hash_size = 0;
    Solver.P_Finish_Counter = 0x14;

    int32_t RetryCount = 0;
    int32_t Result;
    for (;;)
    {
        IVP_Ledge_Mindist* const Mindist = Solver.mindist;
        const int32_t TableIndex = Mindist->get_sorted_synapse(1)->status + Mindist->get_sorted_synapse(0)->status * 4;
        // Retail indexes the flat 16-entry table directly (no range check).
        Result = (&IVP_Mindist_Minimize_Solver::mms_function_table[0][0])[TableIndex](&Solver);

        if (Result == IVP_MRC_OK)
        {
            StateFlags &= 0xffff3fffu;
            return IVP_MRC_OK;
        }

        StateFlags = (StateFlags & 0xffff7fffu) | 0x4000u;
        if (Result == IVP_MRC_ENDLESS_LOOP)
            break;
        if (Result == IVP_MRC_BACKSIDE)
        {
            Solver.proove_polypoly(); // FUN_06352b30
            if (++RetryCount >= 2)
                break;
            continue;
        }
        // The library's CORE trap (:06352da9); retail then repeats the dispatch.
        IVP_CoreTrap();
    }

    if ((StateFlags & 0x3000u) != 0x1000u && (StateFlags & 0x3c0000u) != 0x100000u)
        mindist_rescue_push(); // slot 5

    return Result;
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::recalc_mindist -- FUN_06352b90
// ---------------------------------------------------------------------------
// The library's IVP_Mindist::recalc_mindist. As DispatchNarrowPhaseSolve, but
// P_Finish_Counter = 0 and BACKSIDE runs proove_polypoly once and goes to the
// tail (no retry).
int32_t IVP_Ledge_Mindist::recalc_mindist()
{
    const uint32_t Tick = static_cast<uint32_t>(synapse[0].l_obj->environment->sim_step_count);
    if (LastDispatchTick == Tick)
        return IVP_MRC_ALREADY_CALCULATED;
    LastDispatchTick = Tick;

    IVP_Mindist_Minimize_Solver Solver;
    Solver.mindist = this;
    Solver.loop_hash_size = 0;
    Solver.P_Finish_Counter = 0;

    int32_t Result;
    for (;;)
    {
        IVP_Ledge_Mindist* const Mindist = Solver.mindist;
        const int32_t TableIndex = Mindist->get_sorted_synapse(1)->status + Mindist->get_sorted_synapse(0)->status * 4;
        Result = (&IVP_Mindist_Minimize_Solver::mms_function_table[0][0])[TableIndex](&Solver);

        if (Result == IVP_MRC_OK)
        {
            StateFlags &= 0xffff3fffu;
            return IVP_MRC_OK;
        }

        StateFlags = (StateFlags & 0xffff7fffu) | 0x4000u;
        if (Result == IVP_MRC_ENDLESS_LOOP)
            break;
        if (Result == IVP_MRC_BACKSIDE)
        {
            Solver.proove_polypoly(); // FUN_06352b30
            break;
        }
        // The library's CORE trap (:06352c68); retail then repeats the dispatch.
        IVP_CoreTrap();
    }

    if ((StateFlags & 0x3000u) != 0x1000u && (StateFlags & 0x3c0000u) != 0x100000u)
        mindist_rescue_push(); // slot 5

    return Result;
}

// LAB_06352b10, vtable 063a55e0 slot 5: IVP_Anomaly_Manager slot 2
// (inter_penetration) on object 0's environment.
void IVP_Ledge_Mindist::mindist_rescue_push()
{
    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    Object0->environment->anomaly_manager->inter_penetration(this, Object0, synapse[1].l_obj);
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Base::GetLedges
// ---------------------------------------------------------------------------
// LAB_0634de90, vtable slot 2 of IVP_Mindist_Base (063a55ac), inherited by
// 063a55e0 / 063a5878: each tagged feature masked to its triangle, then
// GetCompactLedge() (inlined in retail). Slot 1, LAB_0634de70, is get_objects.
void IVP_Mindist_Base::GetLedges(const IVP_Compact_Ledge* Out[2]) const
{
    auto RecoverLedge = [](const void* TaggedFeature) -> const IVP_Compact_Ledge*
    {
        const IVP_Compact_Triangle* Triangle = reinterpret_cast<const IVP_Compact_Triangle*>(
            reinterpret_cast<uintptr_t>(TaggedFeature) & ~static_cast<uintptr_t>(0x0F));
        return Triangle->GetCompactLedge();
    };
    Out[0] = RecoverLedge(synapse[0].edge);
    Out[1] = RecoverLedge(synapse[1].edge);
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::exact_mindist_went_invalid
// ---------------------------------------------------------------------------
// LAB_0634e9d0, vtable slot 7 (063a55fc), RET 4. The stack argument is the
// mindist manager and is `this` of both calls.
void IVP_Ledge_Mindist::exact_mindist_went_invalid(IVP_Mindist_Manager* Manager)
{
    Manager->DetachMindist(this);          // FUN_0634fec0
    Manager->insert_invalid_mindist(this); // FUN_0634e960
}

// ===========================================================================
// FUN_0634f320 / FUN_06350140 / FUN_06350050
// ===========================================================================
// Points that are easy to misread:
//   a. FUN_0634f320's final horizon test (:0634f957, against DAT_063a53d4 =
//      0.0f) RETURNS on >= 0: the event is enqueued only when it lands BEFORE
//      the next PSI.
//   b. The StateFlags bits 22-29 update (:0634f592) is a DECREMENT:
//      `EDX = S & 0xffc00000; EDX--; ...` borrows up through the zero low bits.
//   c. FUN_06350140 enters the first FUN_06350050 call when
//      (ObjectA->flags & 7) == 0 (`TEST ...,0x7; JNZ` skips), likewise for B.
// ===========================================================================

namespace
{
    // FUN_0634f320 builds the 0x34-byte IVP_Sweep_Estimator_Context
    // (ivp_ledge_sweep_solver.h) at BASE+0x20 and passes it to
    // IVP_InvokeSweepEstimator (`CALL [EDX*4 + DAT_0658af9c]`, :0634f5ea).
    static_assert(sizeof(IVP_Sweep_Estimator_Context) == 0x34,
                  "estimator context must be retail's 0x34 bytes");

    // Retail's split-time renormalization, inlined six times in FUN_0634f320
    // (:0634f6b8, :0634f702, :0634f78b, :0634f82a, :0634f874, :0634f8fe):
    // carry whole units from Offset into Base while Offset > 1.0 (unordered
    // skips). Both stay on the x87 stack, hence double; the caller rounds
    // where retail does.
    inline void IVP_RenormalizeSplitTime(double& Base, double& Offset)
    {
        while (Offset > 1.0)
        {
            Base += 1.0;
            Offset -= 1.0;
        }
    }

}

// ---------------------------------------------------------------------------
// FUN_06350050
// ---------------------------------------------------------------------------
// Re-keys both sides' hull managers for the slack this pair may still consume
// before its cached mindist stops being valid, and caches the resulting total
// spread on the mindist.
static void IVP_RefreshHullKeysAndSpread(IVP_Ledge_Mindist* M, float SlackA, float SlackB)
{
    M->StateFlags = (M->StateFlags & 0xffd7ffffu) | 0x140000u;

    // Per side, with the time read through the side object's environment:
    //   Elapsed = ((now.base - hull.LastEditTimeHi) + now.offset) - hull.LastEditTimeLo
    //             (x87, `FST float` -> kept as a float for the spread below)
    //   Key     = (Elapsed * OuterGradient + OuterValueAtLastEdit) + Slack (x87 -> float arg)
    //   synapse.HullPoolSlot = hull pool Insert(&synapse, Key)  (06362f90, object +0x64)
    //   Spread  = (OuterGradient - InnerGradient) * Elapsed
    //           + (OuterValueAtLastEdit - InnerValueAtLastEdit)
    // Side 0's spread is stored as a float (:063500e3, into the param_2 slot);
    // side 1's stays on the x87 stack, gets side 0's added and lands in
    // CombinedHullSpread (+0x5c, :06350129).
    float SpreadA = 0.0f;
    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Synapse_Real* const Syn = &M->synapse[Side];
        IVP_Real_Object* const Obj = Syn->l_obj;
        IVP_Hull_Manager& Hull = Obj->hull_manager;
        const IVP_Environment* const Env = Obj->environment;

        const float Elapsed = (float)(
            (((double)(Env->current_time.time_base) - Hull.LastEditTimeHi)
             + Env->current_time.time_offset) - Hull.LastEditTimeLo);
        const float Key = (float)(
            ((double)(Elapsed) * Hull.OuterGradient + Hull.OuterValueAtLastEdit)
            + ((Side == 0) ? SlackA : SlackB));

        Syn->HullPoolSlot = Hull.PoolInsert(static_cast<IVP_Listener_Hull*>(Syn), Key);

        const double Spread = ((double)(Hull.OuterGradient) - Hull.InnerGradient) * Elapsed
                              + ((double)(Hull.OuterValueAtLastEdit) - Hull.InnerValueAtLastEdit);
        if (Side == 0)
            SpreadA = (float)(Spread);
        else
            M->CombinedHullSpread = (float)(Spread + SpreadA);
    }
}

// ---------------------------------------------------------------------------
// FUN_06350140
// ---------------------------------------------------------------------------
// Splits one separation slack between the two sides' hull managers. __cdecl.
static void IVP_DistributeHullSlack(IVP_Ledge_Mindist* M, float Slack)
{
    IVP_Real_Object* const ObjA = M->synapse[0].l_obj;
    IVP_Real_Object* const ObjB = M->synapse[1].l_obj;

    // Taken when the masked flags are ZERO (see (c) above).
    if ((ObjA->flags & 7u) == 0)
    {
        IVP_RefreshHullKeysAndSpread(M, 0.0f, Slack);
        return;
    }
    if ((ObjB->flags & 7u) == 0)
    {
        IVP_RefreshHullKeysAndSpread(M, Slack, 0.0f);
        return;
    }

    const IVP_Core* const CoreA = ObjA->core;
    const IVP_Core* const CoreB = ObjB->core;

    // Both extents stay on the x87 stack. DAT_063a53d8 = 1e-10f, DAT_063a5464
    // = 0.1f.
    const double ExtentA = ((double)(CoreA->MaxSurfaceRotSpeed) + CoreA->CurrentSpeed) + 1.0e-10f;
    const double ExtentB = ((double)(CoreB->MaxSurfaceRotSpeed) + CoreB->CurrentSpeed) + 1.0e-10f;

    // WeightA is spilled as float, WeightB stays extended.
    const float WeightA = (float)(ExtentB * 0.1f + ExtentA);
    const double WeightB = ExtentA * 0.1f + ExtentB;

    // The stack keeps the extended quotient; a float copy is spilled.
    const double ScaleExt = (double)(Slack) / (WeightB + WeightA);
    const float Scale = (float)(ScaleExt);

    // B's slack uses the extended values, A's the float ones.
    const float SlackB = (float)(ScaleExt * WeightB);
    const float SlackA = (float)((double)(Scale) * WeightA);
    IVP_RefreshHullKeysAndSpread(M, SlackA, SlackB);
}

// ---------------------------------------------------------------------------
// FUN_063501f0, cdecl -- the insert half of the library's insert_hull_mindist.
// Per synapse: key = float(hull_manager.AccumulatedDeltaA (object+0x5c) +
// slack), inserted into the hull pool (object+0x64). No spread is written
// (unlike 06350050).
// ---------------------------------------------------------------------------
static void IVP_InsertHullMindistKeys(IVP_Ledge_Mindist* M, float SlackA, float SlackB)
{
    M->StateFlags = (M->StateFlags & 0xffd7ffffu) | 0x140000u;
    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_Synapse_Real* const Syn = &M->synapse[I];
        IVP_Hull_Manager& Hull = Syn->l_obj->hull_manager;
        const float Key = (float)((double)(Hull.AccumulatedDeltaA) + ((I == 0) ? SlackA : SlackB));
        Syn->HullPoolSlot = Hull.PoolInsert(static_cast<IVP_Listener_Hull*>(Syn), Key);
    }
}

// ---------------------------------------------------------------------------
// FUN_06350250, cdecl -- the library's insert_hull_mindist. The twin of
// IVP_DistributeHullSlack (06350140), except that the "other" side of the two
// early branches gets 1e-10f (`PUSH 0x2edbe6ff`) instead of 0, and it ends in
// 063501f0 instead of 06350050. Callers: insert_lazy_mindist 0634eb8a,
// recalc_exact_mindist 06350515, and the recursive mindist (063627ea, 063629xx).
// ---------------------------------------------------------------------------
static void IVP_InsertHullMindist(IVP_Ledge_Mindist* M, float Slack)
{
    IVP_Real_Object* const ObjA = M->synapse[0].l_obj;
    IVP_Real_Object* const ObjB = M->synapse[1].l_obj;

    if ((ObjA->flags & 7u) == 0)
    {
        IVP_InsertHullMindistKeys(M, 1.0e-10f, Slack);
        return;
    }
    if ((ObjB->flags & 7u) == 0)
    {
        IVP_InsertHullMindistKeys(M, Slack, 1.0e-10f);
        return;
    }

    const IVP_Core* const CoreA = ObjA->core;
    const IVP_Core* const CoreB = ObjB->core;
    const double ExtentA = ((double)(CoreA->MaxSurfaceRotSpeed) + CoreA->CurrentSpeed) + 1.0e-10f;
    const double ExtentB = ((double)(CoreB->MaxSurfaceRotSpeed) + CoreB->CurrentSpeed) + 1.0e-10f;
    const float WeightA = (float)(ExtentB * 0.1f + ExtentA);
    const double WeightB = ExtentA * 0.1f + ExtentB;
    const double ScaleExt = (double)(Slack) / (WeightB + WeightA);
    const float Scale = (float)(ScaleExt);
    const float SlackB = (float)(ScaleExt * WeightB);
    const float SlackA = (float)((double)(Scale) * WeightA);
    IVP_InsertHullMindistKeys(M, SlackA, SlackB);
}

// ---------------------------------------------------------------------------
// FUN_0634fec0 -- IVP_Mindist_Manager::DetachMindist (port name)
// ---------------------------------------------------------------------------
// The manager's full detach (RET 4): cancel the mindist's queued collision
// event, unlink it from the manager's list and both synapses from their
// objects' lists, and remove it from ActiveMindists. (At :0634f3fe it is
// followed by IVP_DistributeHullSlack, which is what arms the hull wakeup.)
// Retail dereferences unconditionally.
void IVP_Mindist_Manager::DetachMindist(IVP_Ledge_Mindist* Mindist)
{
    // Through this manager's environment (+0x04) -> sim_unit_manager (+0x04).
    if (Mindist->EventQueueHandle != 0xffff)
    {
        environment->sim_unit_manager->CancelEvent(Mindist->EventQueueHandle); // FUN_063609f0
        Mindist->EventQueueHandle = 0xffff;
    }

    // Only a node whose Prev is null rewrites the head, which fixes +0x74 as
    // next and +0x78 as prev.
    if (Mindist->ManagerNext != nullptr)
        Mindist->ManagerNext->ManagerPrev = Mindist->ManagerPrev;
    if (Mindist->ManagerPrev != nullptr)
        Mindist->ManagerPrev->ManagerNext = Mindist->ManagerNext;
    else
        ActiveMindistHead = Mindist->ManagerNext;

    // The null-prev branch writes the head of the slot's own object.
    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_Synapse_Real* const Syn = &Mindist->synapse[I];
        if (Syn->next != nullptr)
            Syn->next->prev = Syn->prev;
        if (Syn->prev != nullptr)
            Syn->prev->next = Syn->next;
        else
            Syn->l_obj->synapse_list_head = Syn->next;
    }

    // Backward scan, first (highest-index) match only, swap-with-last removal
    // (RemoveAtAndAllowSort, not RemoveElement's shift).
    for (int Index = static_cast<int>(ActiveMindists.n_elems) - 1; Index >= 0; --Index)
    {
        if (ActiveMindists.elems[Index] == Mindist)
        {
            ActiveMindists.RemoveAtAndAllowSort(Index);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Manager::insert_exact_mindist -- FUN_0634e9f0, RET 4.
// ---------------------------------------------------------------------------
namespace
{
    // Pushes a synapse onto the head of an object's synapse list (exact list
    // object+0x20, invalid list object+0x24; inlined in retail).
    void IVP_PushSynapseNode(IVP_Synapse_Real* Slot, IVP_Synapse_Real** Head)
    {
        IVP_Synapse_Real* OldHead = *Head;
        Slot->next = OldHead;
        if (OldHead != nullptr)
            OldHead->prev = Slot;
        Slot->prev = nullptr;
        *Head = Slot;
    }

    IVP_Synapse_Real* IVP_SynapseSlotA(IVP_Ledge_Mindist* Mindist)
    {
        return &Mindist->synapse[0]; // mindist+0x18
    }

    IVP_Synapse_Real* IVP_SynapseSlotB(IVP_Ledge_Mindist* Mindist)
    {
        return &Mindist->synapse[1]; // mindist+0x34
    }
}

void IVP_Mindist_Manager::insert_exact_mindist(IVP_Ledge_Mindist* Mindist)
{
    // Clear bits 20/21, set bits 18/19.
    Mindist->StateFlags = (Mindist->StateFlags & 0xffcfffffu) | 0xc0000u;

    Mindist->ManagerNext = ActiveMindistHead;
    Mindist->ManagerPrev = nullptr;
    if (Mindist->ManagerNext != nullptr)
        Mindist->ManagerNext->ManagerPrev = Mindist;

    IVP_PushSynapseNode(IVP_SynapseSlotA(Mindist), &Mindist->synapse[0].l_obj->synapse_list_head);
    IVP_PushSynapseNode(IVP_SynapseSlotB(Mindist), &Mindist->synapse[1].l_obj->synapse_list_head);

    ActiveMindistHead = Mindist;
    Mindist->DispatchNarrowPhaseSolve(); // FUN_06352cf0

    // Either core has an AxisLock (+0x10) -> also keep it in ActiveMindists.
    if (Mindist->synapse[0].l_obj->core->AxisLock != nullptr || Mindist->synapse[1].l_obj->core->AxisLock != nullptr)
        ActiveMindists.Add(Mindist);

    if ((Mindist->StateFlags & 0xc000u) != 0)
    {
        Mindist->exact_mindist_went_invalid(this); // slot 7
        return;
    }
    // Retail: (coreB->MovementState | coreA->MovementState) << 24, signed
    // `< 0x21000000` -- the signed low byte of the OR (ivp_signed_byte.h).
    const int32_t Movement = IVP_SignedLowByte(
        Mindist->synapse[1].l_obj->core->MovementState | Mindist->synapse[0].l_obj->core->MovementState);
    Mindist->RescheduleCollisionEvent((Movement < 0x21) ? 1 : 0, 0);
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Manager::insert_lazy_mindist -- FUN_0634eb00, RET 4.
//   recalc_mindist (result unused); unless invalid (flags & 0xc000), a
//   strictly positive SeparationGap goes straight to the exact list
//   (leaving the phantoms first) and reschedules with (1, 2). Otherwise
//   slack = -SeparationGap (or CombinedRadius when invalid), widened by the
//   first existing phantom's exit_policy_extra_radius (+0x4), and
//   insert_hull_mindist. A phantom client_data (+0x40) of 1 sends a recursive
//   mindist to 06362840; otherwise the mindist is added to the phantoms unless
//   it already is.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::insert_lazy_mindist(IVP_Ledge_Mindist* Mindist)
{
    Mindist->recalc_mindist(); // FUN_06352b90

    float Slack;
    if ((Mindist->StateFlags & 0xc000u) == 0)
    {
        if (Mindist->SeparationGap > 0.0f) // not-greater or unordered falls through
        {
            if ((Mindist->StateFlags & 0xc00u) != 0)
                IVP_RemoveMindistFromPhantoms(Mindist);
            insert_exact_mindist_no_recalc(Mindist);
            Mindist->RescheduleCollisionEvent(1, 2);
            return;
        }
        Slack = -Mindist->SeparationGap;
    }
    else
    {
        Slack = Mindist->CombinedRadius;
    }

    int32_t PhantomFlag = 0;
    const IVP_Controller_Phantom* Phantom = static_cast<const IVP_Controller_Phantom*>(Mindist->synapse[0].l_obj->controller_phantom);
    if (Phantom == nullptr)
        Phantom = static_cast<const IVP_Controller_Phantom*>(Mindist->synapse[1].l_obj->controller_phantom);
    if (Phantom != nullptr)
    {
        PhantomFlag = static_cast<int32_t>(reinterpret_cast<intptr_t>(Phantom->client_data));
        Slack = (float)((double)(Slack) + Phantom->exit_policy_extra_radius);
    }

    IVP_InsertHullMindist(Mindist, Slack); // FUN_06350250

    if (PhantomFlag == 1)
    {
        if (Mindist->is_recursive() != 0)
        {
            static_cast<IVP_Complex_Mindist*>(Mindist)->RecalcWinnerLedgePair(this); // FUN_06362840
            return;
        }
    }
    else if ((Mindist->StateFlags & 0xc00u) != 0)
    {
        return;
    }
    IVP_AddMindistToPhantoms(Mindist);
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Manager::insert_invalid_mindist -- FUN_0634e960, RET 4.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::insert_invalid_mindist(IVP_Ledge_Mindist* Mindist)
{
    // Clear bits 18/20/21, set bit 19.
    Mindist->StateFlags = (Mindist->StateFlags & 0xffcbffffu) | 0x80000u;

    // The invalid list (+0x14).
    Mindist->ManagerNext = InvalidMindistHead;
    Mindist->ManagerPrev = nullptr;
    if (InvalidMindistHead != nullptr)
        InvalidMindistHead->ManagerPrev = Mindist;
    InvalidMindistHead = Mindist;

    IVP_PushSynapseNode(IVP_SynapseSlotA(Mindist), &Mindist->synapse[0].l_obj->invalid_synapse_list_head);
    IVP_PushSynapseNode(IVP_SynapseSlotB(Mindist), &Mindist->synapse[1].l_obj->invalid_synapse_list_head);
}

// ---------------------------------------------------------------------------
// FUN_0634e8a0, RET 4. The linking half of insert_exact_mindist (0634e9f0)
// without its narrow-phase pass and event scheduling. Callers: 0634549d,
// insert_lazy_mindist (0634eb38) and the recursive mindist (06362d7d).
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::insert_exact_mindist_no_recalc(IVP_Ledge_Mindist* Mindist)
{
    Mindist->StateFlags = (Mindist->StateFlags & 0xffcfffffu) | 0xc0000u;

    // The head is written before the synapses here (0634e9f0 writes it after).
    Mindist->ManagerNext = ActiveMindistHead;
    Mindist->ManagerPrev = nullptr;
    if (ActiveMindistHead != nullptr)
        ActiveMindistHead->ManagerPrev = Mindist;
    ActiveMindistHead = Mindist;

    IVP_PushSynapseNode(IVP_SynapseSlotA(Mindist), &Mindist->synapse[0].l_obj->synapse_list_head);
    IVP_PushSynapseNode(IVP_SynapseSlotB(Mindist), &Mindist->synapse[1].l_obj->synapse_list_head);

    if (Mindist->synapse[0].l_obj->core->AxisLock != nullptr || Mindist->synapse[1].l_obj->core->AxisLock != nullptr)
        ActiveMindists.Add(Mindist);
}

// ---------------------------------------------------------------------------
// FUN_0634ffa0, RET 4. The inverse of insert_invalid_mindist (0634e960).
// Callers: 06345495 and 0634dddd.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::remove_invalid_mindist(IVP_Ledge_Mindist* Mindist)
{
    if (Mindist->ManagerNext != nullptr)
        Mindist->ManagerNext->ManagerPrev = Mindist->ManagerPrev;
    if (Mindist->ManagerPrev != nullptr)
        Mindist->ManagerPrev->ManagerNext = Mindist->ManagerNext;
    else
        InvalidMindistHead = Mindist->ManagerNext;

    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_Synapse_Real* const Slot = &Mindist->synapse[I];
        if (Slot->next != nullptr)
            Slot->next->prev = Slot->prev;
        if (Slot->prev != nullptr)
            Slot->prev->next = Slot->next;
        else
            Slot->l_obj->invalid_synapse_list_head = Slot->next;
    }
}

// ---------------------------------------------------------------------------
// FUN_06350020, RET 4 -- remove_hull_mindist: FUN_06363280 on each object's
// hull pool (object+0x64 = hull_manager (+0x44) + 0x20) with the synapse's
// pool slot.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::remove_hull_mindist(IVP_Ledge_Mindist* Mindist)
{
    Mindist->synapse[0].l_obj->hull_manager.PoolRemove(static_cast<uint16_t>(Mindist->synapse[0].HullPoolSlot));
    Mindist->synapse[1].l_obj->hull_manager.PoolRemove(static_cast<uint16_t>(Mindist->synapse[1].HullPoolSlot));
}

// ---------------------------------------------------------------------------
// FUN_06350460, RET 4 -- the library's recalc_exact_mindist.
//   After the narrow phase: no 0x3000 state -> invalidate when 0xc000, done.
//   Otherwise value = CombinedRadius when invalid, else a strictly positive
//   SeparationGap leaves the phantoms and returns, else value = -Gap.
//   Phantom: object 0's; if both objects have one, return; when object 0's is
//   null, object 1's is read WITHOUT a null test (:063504de).
//   A recursive mindist with phantom client_data 1 is invalidated; otherwise
//   (unless already in the phantoms) add to phantoms, DetachMindist and
//   insert_hull_mindist(value + exit_policy_extra_radius).
// Callers: 063454cb, recalc_all_exact_wheel_mindist 0634f259,
// recalc_all_exact_mindists 0634f311.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::recalc_exact_mindist(IVP_Ledge_Mindist* Mindist)
{
    Mindist->DispatchNarrowPhaseSolve();

    const uint32_t Flags = Mindist->StateFlags; // read once
    if ((Flags & 0x3000u) == 0)
    {
        if ((Flags & 0xc000u) != 0)
            Mindist->exact_mindist_went_invalid(this);
        return;
    }

    float Value;
    if ((Flags & 0xc000u) != 0)
    {
        Value = Mindist->CombinedRadius;
    }
    else if (Mindist->SeparationGap > 0.0f)
    {
        if ((Flags & 0xc00u) != 0)
            IVP_RemoveMindistFromPhantoms(Mindist);
        return;
    }
    else
    {
        Value = -Mindist->SeparationGap;
    }

    const IVP_Controller_Phantom* const Phantom0 = static_cast<const IVP_Controller_Phantom*>(Mindist->synapse[0].l_obj->controller_phantom);
    const IVP_Controller_Phantom* Phantom = Phantom0;
    if (Phantom0 != nullptr)
    {
        if (Mindist->synapse[1].l_obj->controller_phantom != nullptr)
            return;
    }
    else
    {
        Phantom = static_cast<const IVP_Controller_Phantom*>(Mindist->synapse[1].l_obj->controller_phantom); // no null test
    }

    const float Slack = (float)((double)(Value) + Phantom->exit_policy_extra_radius);
    const int32_t PhantomFlag = static_cast<int32_t>(reinterpret_cast<intptr_t>(Phantom->client_data));

    if (Mindist->is_recursive() != 0 && PhantomFlag == 1)
    {
        Mindist->exact_mindist_went_invalid(this);
        return;
    }

    if ((Mindist->StateFlags & 0xc00u) != 0) // re-read
        return;
    IVP_AddMindistToPhantoms(Mindist);
    DetachMindist(Mindist);
    IVP_InsertHullMindist(Mindist, Slack);
}

// ---------------------------------------------------------------------------
// FUN_0634f300 -- the library's recalc_all_exact_mindists: next (+0x74) is
// read before recalc_exact_mindist may unlink the node.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::recalc_all_exact_mindists()
{
    IVP_Ledge_Mindist* Mindist = ActiveMindistHead;
    while (Mindist != nullptr)
    {
        IVP_Ledge_Mindist* const Next = Mindist->ManagerNext;
        recalc_exact_mindist(Mindist);
        Mindist = Next;
    }
}

namespace
{
    // FUN_0634fd10 (thiscall on the mindist, RET 8), Ipion
    // IVP_Mindist::create_cp_in_advance_pretension. With no contact point on
    // the object it returns before the arena bracket. Old is the object's
    // first contact point, read before 06356bc0 runs; its time stamp is set to
    // now for its frame update (0635a1e0) and then restored. The new point
    // inherits Old's pressure (Old's is zeroed), stamp and Gap, and Old's
    // friction span carried through world space onto the new tangent axes
    // (mixed float/unrounded intermediates as below).
    void IVP_Create_Cp_In_Advance_Pretension(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* Object, float Gap)
    {
        char* const Node = static_cast<char*>(Object->mindist_list);
        if (Node == nullptr)
        {
            return;
        }

        ++Object->environment->sim_unit_mem->State;
        IVP_ListManager* FrictionSystem = nullptr;
        int32_t HavingNew = 0;
        IVP_Contact_Point* const Cp =
            Mindist->try_to_generate_managed_friction(&FrictionSystem, &HavingNew, nullptr, 1);
        if (HavingNew == 1)
        {
            IVP_Contact_Point* const Old = reinterpret_cast<IVP_Contact_Point*>(
                Node + *reinterpret_cast<const int16_t*>(Node + 0xc));
            const float SavedRecalcTimeBase = Old->LastRecalcTimeBase;
            const float SavedRecalcTimeOffset = Old->LastRecalcTimeOffset;
            IVP_Environment* const Env = Object->environment;
            std::memcpy(&Old->LastRecalcTimeBase, &Env->current_time.time_base, sizeof(float));   // raw dword copy
            std::memcpy(&Old->LastRecalcTimeOffset, &Env->current_time.time_offset, sizeof(float));
            Old->UpdateContactFrame(); // FUN_0635a1e0

            Cp->NowFrictionPressure = Old->NowFrictionPressure;
            Old->NowFrictionPressure = 0.0f;
            Cp->LastRecalcTimeBase = SavedRecalcTimeBase;
            Cp->GapDistance = Gap;
            Cp->LastRecalcTimeOffset = SavedRecalcTimeOffset;

            const IVP_Impact_Solver_Long_Term* const OldOwner = Old->Owner;
            const double S0 = (double)(Old->SpanFrictionS[0]);
            const double S1 = (double)(Old->SpanFrictionS[1]);
            Old->LastRecalcTimeBase = SavedRecalcTimeBase;
            Old->LastRecalcTimeOffset = SavedRecalcTimeOffset;

            const float X0 = (float)(S0 * OldOwner->TangentAxis0.x);
            const float Y0 = (float)(S0 * OldOwner->TangentAxis0.y);
            const double Z = S0 * OldOwner->TangentAxis0.z + S1 * OldOwner->TangentAxis1.z;
            const float Y = (float)(S1 * OldOwner->TangentAxis1.y + (double)(Y0));
            const float X = (float)(S1 * OldOwner->TangentAxis1.x + (double)(X0));

            const IVP_Impact_Solver_Long_Term* const NewOwner = Cp->Owner;
            const double Span1 = (Z * NewOwner->TangentAxis1.z + (double)(Y) * NewOwner->TangentAxis1.y) +
                                 (double)(X) * NewOwner->TangentAxis1.x;
            const double Span0 = (Z * NewOwner->TangentAxis0.z + (double)(Y) * NewOwner->TangentAxis0.y) +
                                 (double)(X) * NewOwner->TangentAxis0.x;
            Cp->SpanFrictionS[0] = (float)(Span0);
            Cp->SpanFrictionS[1] = (float)(Span1);
        }

        IVP_U_Memory* const Arena = Object->environment->sim_unit_mem;
        --Arena->State;
        Arena->reset_chunks(); // FUN_06344070
    }
}

// ---------------------------------------------------------------------------
// FUN_0634f230 -- the library's recalc_all_exact_wheel_mindist, backward over
// ActiveMindists (element re-read by index each round). The sorted synapses
// are swapped unless synapse 0 is the ball. A ball-vs-poly pair with a new
// poly edge (vs +0x7c), SeparationGap < DAT_0658ab2c (max_distance_for_friction;
// unordered proceeds) and a plain poly ledge gets a pretension contact point
// (FUN_0634fd10).
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::recalc_all_exact_wheel_mindist()
{
    for (int32_t Index = static_cast<int32_t>(ActiveMindists.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Ledge_Mindist* const Mindist = ActiveMindists.elems[Index];
        recalc_exact_mindist(Mindist);

        IVP_Synapse_Real* Ball = Mindist->get_sorted_synapse(0);
        IVP_Synapse_Real* Other = Mindist->get_sorted_synapse(1);
        if (Ball->status != IVP_SYNAPSE_BALL)
        {
            IVP_Synapse_Real* const Tmp = Ball;
            Ball = Other;
            Other = Tmp;
        }

        if (Other->edge == Mindist->LastPretensionEdge)
            continue;
        if (Other->status != IVP_SYNAPSE_POLY)
            continue;
        if (Ball->status != IVP_SYNAPSE_BALL)
            continue;
        const float Gap = Mindist->SeparationGap;
        if (Gap >= G_IVP_MindistSettings.max_distance_for_friction)
            continue;
        if ((Other->get_ledge()->SizeAndFlags & IVP_Compact_Ledge::kHasChildrenMask) != 0)
            continue;

        IVP_Create_Cp_In_Advance_Pretension(Mindist, Ball->l_obj, Gap); // FUN_0634fd10
        Mindist->LastPretensionEdge = Other->edge;
    }
}

// ---------------------------------------------------------------------------
// FUN_0634f210: FUN_0634f320(1, 1) over the exact list, next read first.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::recalc_all_exact_mindists_events()
{
    IVP_Ledge_Mindist* Mindist = ActiveMindistHead;
    while (Mindist != nullptr)
    {
        IVP_Ledge_Mindist* Next = Mindist->ManagerNext;
        Mindist->RescheduleCollisionEvent(1, 1);
        Mindist = Next;
    }
}

// ---------------------------------------------------------------------------
// FUN_0634f320 -- IVP_Ledge_Mindist::RescheduleCollisionEvent (port name)
// ---------------------------------------------------------------------------
// Thiscall, RET 0x8. RearmWhenFar is the first stack argument. Call sites:
// UpdatePerTick (:0634fea2 / :0634feb1, (0, 2) / (0, 1)), :06344f98,
// :0634eadb, :0634eb43, :0634f21f.
//
// Retail reuses the first argument's slot as float scratch from :0634f4a3
// (RearmWhenFar is live only at :0634f3ee) and the second's (:0634f69b) to
// cache Env->delta_time; EventMode is read once, at :0634f621.
void IVP_Ledge_Mindist::RescheduleCollisionEvent(int32_t RearmWhenFar, int32_t EventMode)
{
    // 1. `XOR EAX,0x100` flips bit 8 only, so the indices are always i and
    //    i^1 (synapse slots at this + 0x28 + index*0x1c). No null tests.
    const uint32_t FlagsAtEntry = StateFlags;
    const int32_t SideI = static_cast<int32_t>((FlagsAtEntry >> 8) & 3u);
    const int32_t SideJ = static_cast<int32_t>(((FlagsAtEntry ^ 0x100u) >> 8) & 3u);

    IVP_Real_Object* const ObjectI = synapse[SideI].l_obj;
    IVP_Real_Object* const ObjectJ = synapse[SideJ].l_obj;

    const IVP_Core* const CoreI = ObjectI->core; // retail EDI
    const IVP_Core* const CoreJ = ObjectJ->core; // retail EBX
    IVP_Environment* const Env = synapse[0].l_obj->environment; // retail EBP -- slot 0, NOT SideI

    // 2. Each sum rounded once. DAT_063a561c = 2e-10f.
    const float SurfaceRotSpeedSum = (float)((double)(CoreI->MaxSurfaceRotSpeed) + CoreJ->MaxSurfaceRotSpeed);
    const float MaxSpeedSum = (float)(
        (((double)(CoreJ->CurrentSpeed) + CoreI->CurrentSpeed) + SurfaceRotSpeedSum) + 2.0e-10f);

    // 3. De-queue whatever slot this pair holds (env+0x04, FUN_063609f0).
    if (EventQueueHandle != 0xffff)
    {
        Env->sim_unit_manager->CancelEvent(EventQueueHandle); // FUN_063609f0
        EventQueueHandle = 0xffff;
    }

    // 4. COARSE GATE (:0634f3ae): threshold < gap, or unordered, is the FAR
    //    case. DAT_063a5618 = 2.1f. The ramp value is reused at :0634f406.
    const uint32_t FlagsAfterDequeue = StateFlags; // re-read
    const uint32_t ToleranceIndex = (FlagsAfterDequeue >> 22) & 0xffu;
    const float ToleranceValue = IVP_ToleranceRamp(ToleranceIndex);
    const double CoarseThreshold = (((double)(Env->delta_time) * MaxSpeedSum) * 2.1f) + ToleranceValue;
    const float Gap = SeparationGap;

    if (!(CoarseThreshold >= (double)(Gap)))
    {
        // 5. Too far to schedule this step; if asked, re-arm the hull-distance
        //    watchdog so the pair wakes once the objects close by that much.
        if (RearmWhenFar == 0)
            return;
        Env->mindist_manager->DetachMindist(this); // FUN_0634fec0
        IVP_DistributeHullSlack(this, (float)((double)(SeparationGap) - ToleranceValue));
        return;
    }

    // 6. CLOSING-SPEED BOUND, retail's operand order. DAT_063a5614 = 1.001f.
    //    SpinI stays on the stack; SpinJ is spilled as a float.
    const Vector& N = SeparatingNormal;
    const double DotSpeedJ = ((double)(N.z) * CoreJ->Speed.z + (double)(N.y) * CoreJ->Speed.y) +
                             (double)(CoreJ->Speed.x) * N.x;
    const double DotSpeedI = ((double)(N.z) * CoreI->Speed.z + (double)(N.y) * CoreI->Speed.y) +
                             (double)(N.x) * CoreI->Speed.x;
    const float RelativeSpeed = (float)(DotSpeedJ - DotSpeedI);

    const double SpinI = ((double)(CoreI->RotAxisWS.z) * N.z + (double)(CoreI->RotAxisWS.y) * N.y) +
                         (double)(CoreI->RotAxisWS.x) * N.x;
    const float SpinJ = (float)(
        ((double)(CoreJ->RotAxisWS.z) * N.z + (double)(CoreJ->RotAxisWS.y) * N.y) +
        (double)(CoreJ->RotAxisWS.x) * N.x);
    const double RotTermI = sqrt(1.001f - SpinI * SpinI) * CoreI->MaxSurfaceRotSpeed;
    const double RotTermJ = sqrt(1.001f - (double)(SpinJ) * SpinJ) * CoreJ->MaxSurfaceRotSpeed;
    const float ClosingBound = (float)((RotTermI + RotTermJ) + RelativeSpeed);

    // 7. HORIZON TEST. The four time words are dword copies. DAT_063a53d8 = 1e-10f.
    const float NowHi = Env->current_time.time_base;
    const float NowLo = Env->current_time.time_offset;
    const float NextHi = Env->next_psi_time_base;
    const float NextLo = Env->next_psi_time_offset;

    // Less than MinRate (or unordered) re-tests against 1e-10f.
    if (!(ClosingBound >= IVP_ToleranceMinRate()) && !(ClosingBound >= 1.0e-10f))
        return; // nothing is approaching fast enough to matter

    // Horizon unrounded; only horizon > gap goes on.
    const double Horizon = (((((double)(NextHi) - NowHi) + NextLo) - NowLo) * ClosingBound) + ToleranceValue;
    if (!(Horizon > (double)(Gap)))
        return;

    // 8.
    if ((FlagsAfterDequeue & 0x3000u) == 0x1000u)
        return;

    // `TEST` clears CF, so the `JBE` at :0634f579 is a plain JZ.
    if ((FlagsAfterDequeue & 0x3fc00000u) != 0)
    {
        int32_t& Counter = IVP_ToleranceRecomputeCounter(); // DAT_0658ab44
        const int32_t Previous = Counter;
        Counter = Previous + 1;
        if (Previous > 2)
        {
            // In-place DECREMENT of the 8-bit tolerance index in bits 22-29
            // (fresh read of +0x14); wraps at zero, as retail does.
            const uint32_t S = StateFlags;
            uint32_t Updated = (S & 0xffc00000u) - 1u;
            Updated ^= S;
            Updated &= 0x3fc00000u;
            Updated ^= S;
            StateFlags = Updated;
            Counter = 0;
        }
    }

    // 9. THE DISPATCH. StateFlags re-read (step 8 may have changed it);
    //    index = ShapeType[j] + ShapeType[i]*4.
    const uint32_t FlagsAtDispatch = StateFlags;
    const int32_t DispatchJ = static_cast<int32_t>(((FlagsAtDispatch ^ 0x100u) >> 8) & 3u);
    const int32_t DispatchI = static_cast<int32_t>((FlagsAtDispatch >> 8) & 3u);
    const int32_t ShapeJ = synapse[DispatchJ].status;
    const int32_t ShapeI = synapse[DispatchI].status;

    // The context on retail's frame at ESP+0x20.
    IVP_Sweep_Estimator_Context Ctx;
    Ctx.SurfaceRotSpeedSum = SurfaceRotSpeedSum;
    Ctx.RelativeSpeed = RelativeSpeed;
    Ctx.ClosingBound = ClosingBound;
    Ctx.MaxSpeedSum = MaxSpeedSum;
    Ctx.Mindist = this;
    Ctx.Env = Env;
    Ctx.NowBase = NowHi;
    Ctx.NowOffset = NowLo;
    Ctx.NextBase = NextHi;
    Ctx.NextOffset = NextLo;
    // DEVIATION (no behavioural difference): retail leaves +0x28..+0x30
    // uninitialised (every handler zeroes OutCode itself).
    Ctx.OutCode = 0;
    Ctx.OutEvent.time_base = 0.0f;
    Ctx.OutEvent.time_offset = 0.0f;

    IVP_InvokeSweepEstimator(ShapeJ + ShapeI * 4, &Ctx);

    if (Ctx.OutCode == 0)
        return;

    // 10. RESULT POST-PROCESSING. DAT_063a5610 = 1e-6f.
    float EventHi = Ctx.OutEvent.time_base;
    float EventLo = Ctx.OutEvent.time_offset;

    // Not-less enqueues as is; less or unordered goes on.
    const double RawLead = (((double)(Ctx.OutEvent.time_base) - Ctx.NowBase) + Ctx.OutEvent.time_offset) - Ctx.NowOffset;
    if (RawLead >= 1.0e-6f)
    {
        // Far enough ahead to use as-is; retail jumps straight to the enqueue.
    }
    else if (EventMode == 0)
    {
        // Clamp to "now" (the context copies) and enqueue, skipping the
        // epsilon nudge and the final horizon check.
        EventHi = Ctx.NowBase;
        EventLo = Ctx.NowOffset;
    }
    else
    {
        // DAT_0658aa10 is the tolerance block's [0x00].
        const float Slack = (float)((double)(Gap) - IVP_ToleranceBase());
        const float DeltaTime = Env->delta_time;
        const float EpsScale = IVP_ToleranceEpsScale(); // DAT_0658ab40, == 1.0f

        // Two epsilon ladders; the second test is the low nibble of the
        // estimator's out-code.
        const bool CoarseLadder = (EventMode == 2) || ((Ctx.OutCode & 0x0f) != 0);

        // DAT_063a547c = 1e-4f, DAT_063a5604 = 1e-3f, DAT_063a5608 = 1e-5f,
        // DAT_063a560c = 1e-7f, DAT_063a5464 = 0.1f.
        const float NudgeEpsilon = CoarseLadder
                                       ? (Slack >= 1.0e-6f ? 1.0e-4f : 1.0e-3f)
                                       : (Slack >= 1.0e-6f ? 1.0e-7f : 1.0e-5f);

        // All four arms re-read the environment's current time (env+0xf8)
        // rather than the context copies. The base lives on the x87 stack
        // through both carry loops; the first offset is rounded before the
        // epsilon is added, and the final pair at the end.
        double Base = Env->current_time.time_base;
        float Offset;
        if (Slack >= 1.0e-6f) // less or unordered skips the gap term
        {
            // Fine ladder: slack * 0.1f / MaxSpeedSum; coarse: slack / MaxSpeedSum.
            double FirstOffset = (CoarseLadder ? ((double)(Slack) / Ctx.MaxSpeedSum)
                                               : (((double)(Slack) * 0.1f) / Ctx.MaxSpeedSum))
                                 + Env->current_time.time_offset;
            IVP_RenormalizeSplitTime(Base, FirstOffset);
            Offset = (float)(FirstOffset);
        }
        else
        {
            // No gap term, just the epsilon.
            Offset = Env->current_time.time_offset;
        }

        double SecondOffset = (((double)(EpsScale) * NudgeEpsilon) * DeltaTime) + Offset;
        IVP_RenormalizeSplitTime(Base, SecondOffset);
        EventHi = (float)(Base);
        EventLo = (float)(SecondOffset);

        // 11. Next-PSI pair re-read (env+0x100 / +0x104); returns on >= 0
        //     (see (a) above), less or unordered enqueues.
        if (((((double)(EventHi) - Env->next_psi_time_base) + EventLo) - Env->next_psi_time_offset) >= 0.0)
            return;
    }

    // 12. ENQUEUE (FUN_06362f90 on the pool pointer at sim_unit_manager+0x08,
    //     key rounded once), then fold the out-code into StateFlags' low byte.
    EventQueueHandle = static_cast<int32_t>(
        Env->sim_unit_manager->EnqueueEvent(this, EventHi, EventLo));
    const uint32_t S = StateFlags;
    StateFlags = S ^ ((S ^ static_cast<uint32_t>(Ctx.OutCode)) & 0xffu);
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::UpdatePerTick
// ---------------------------------------------------------------------------
// FUN_0634fe60, vtable slot 0 of both 063a55e0 and 063a5878.
//   Narrow phase; invalid (0xc000) -> return; a pending out-code in the low
//   nibble -> reschedule (0, 1); otherwise ramp[(flags >> 22) & 0xff] +
//   DAT_0658ab34 (unrounded) > SeparationGap -> virtual do_impact (slot 8),
//   else (<= or unordered) reschedule (0, 2).
void IVP_Ledge_Mindist::UpdatePerTick()
{
    DispatchNarrowPhaseSolve(); // FUN_06352cf0

    if ((StateFlags & 0xc000u) != 0)
        return;

    if ((StateFlags & 0x0Fu) != 0)
    {
        RescheduleCollisionEvent(0, 1);
        return;
    }

    const uint32_t Index = (StateFlags >> 22) & 0xffu;
    const double Threshold = (double)(IVP_ToleranceRamp(Index)) + IVP_ToleranceBias();
    if (Threshold > (double)(SeparationGap))
        do_impact(); // virtual slot 8
    else
        RescheduleCollisionEvent(0, 2);
}

// ---------------------------------------------------------------------------
// IVP_Complex_Mindist
// ---------------------------------------------------------------------------
// The hull pool (FUN_06362ef0/f70/f90/06363280) is IVP_Priority_Pool inside
// each object's hull manager (ivp_real_object.h).

// FUN_06362ab0, RET 8: base FUN_0634dd10, the +0x80 IVP_Collision_Delegator
// sub-object, an empty Children vector (+0x88), SpawnedMindistCount (+0x90) = 0,
// WinnerFlag (+0x84) = -1.
IVP_Complex_Mindist::IVP_Complex_Mindist(IVP_Environment* Environment, IVP_Collision_Delegator* Delegator)
    : IVP_Ledge_Mindist(Environment, Delegator)
    , IVP_Collision_Delegator()
{
    Children.memsize = 0;
    Children.n_elems = 0;
    Children.elems = nullptr;
    SpawnedMindistCount = 0;
    WinnerFlag = -1;
}

// FUN_06362b50 (deleting wrapper FUN_06362b30). Retail's second vector clear
// after FUN_06362a30 is Children's own destructor here; the delegator
// sub-object's dtor (vptr := 063a5750) is the IVP_Collision_Delegator base
// dtor; FUN_0634dd60's base teardown is ~IVP_Ledge_Mindist.
IVP_Complex_Mindist::~IVP_Complex_Mindist()
{
    ClearChildren();
}

// FUN_06362840, RET 4. Caller insert_lazy_mindist (:0634eba6) pushes the
// manager; retail never reads it. Over the spawn cap -> return; else
// WinnerFlag, the intra-object collision range (range_manager slot 0),
// StateFlags = (x & 0xffd3ffff) | 0x100000, SpawnOrDeferChild(R0 + R1). No
// DetachMindist and no insert_hull_mindist here (unlike 06362650/06362920).
void IVP_Complex_Mindist::RecalcWinnerLedgePair(IVP_Mindist_Manager* /*Manager*/)
{
    // DAT_0658ab3c is IVP_Mindist_Settings+0x12c, which PhySys world init
    // copies from PhySys::MAX_SPAWNED_MINDIST_COUNT (:0628c997); retail does
    // not read the PhySys global here.
    if (get_spawned_mindist_count() > G_IVP_MindistSettings.max_spawned_mindist_count) return;

    WinnerFlag = RecalcWinnerFlag(synapse[0].edge, synapse[1].edge);

    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    float Range0;
    float Range1;
    Object0->environment->range_manager->get_coll_range_intra_objects(Object0, synapse[1].l_obj, &Range0, &Range1);
    StateFlags = (StateFlags & 0xffd3ffffu) | 0x100000u;
    SpawnOrDeferChild((float)((double)(Range0) + Range1));
}

// LAB_06362650, vtable 063a5878 slot 8 (063a5898): this class's do_impact
// override, dispatched by UpdatePerTick. Two jump tables
// (switchdataD_0636281c on ShapeTypeA, switchdataD_0636282c on ShapeTypeB).
//   Over the spawn cap -> hand-off.
//   "Hand-off" = a direct call of the base IVP_Ledge_Mindist::do_impact
//     (FUN_06358cb0, :06362797), skipping the epilogue.
//   A outside 0..3 (unsigned JA) -> null-store trap (:063627b1) -> epilogue.
//   A = 0 / 3 (:0636268d): B outside 0..3 -> trap; B = 0 / 3 -> hand-off;
//     B = 1 (:063626a1) FeatureB's own bit 31 (unmasked), B = 2 (:063626ac)
//     FeatureB's triangle bit 31 (masked): set -> WinnerFlag 1, clear ->
//     hand-off.
//   A = 1 (:063626cd): B = 0 / 3 (:06362773) FeatureA's triangle bit 31:
//     set -> 0, clear -> hand-off. B = 1 (:063626f2): own bits of B then A:
//     B clear & A set -> 0, both clear -> hand-off, B set & A clear -> 1,
//     both set -> radius(ledgeA) <= radius(ledgeB) or unordered -> 1,
//     A > B -> 0.
//     Any other B -> :063626e7 null-store trap (EDX = 0) -> epilogue.
//   A = 2 (:06362789): FeatureA's triangle bit 31: set -> 0, clear -> hand-off.
//   Epilogue LAB_063627bb: DetachMindist (env's manager), the intra-object
//     range, insert_hull_mindist(R0 + R1), StateFlags = (x & 0xffd3ffff) |
//     0x100000, SpawnOrDeferChild(R0 + R1).
void IVP_Complex_Mindist::do_impact()
{
    if (get_spawned_mindist_count() > G_IVP_MindistSettings.max_spawned_mindist_count) // DAT_0658ab3c
    {
        IVP_Ledge_Mindist::do_impact();
        return;
    }

    const int16_t ShapeTypeA = synapse[0].status;
    const int16_t ShapeTypeB = synapse[1].status;
    IVP_Compact_Edge* const FeatureA = synapse[0].edge;
    IVP_Compact_Edge* const FeatureB = synapse[1].edge;

    if (ShapeTypeA < IVP_SYNAPSE_POINT || ShapeTypeA > IVP_SYNAPSE_BALL)
    {
        // Retail's release IVP_ASSERT: a deliberate store to address zero.
        IVP_CoreTrap();
    }
    else if (ShapeTypeA == IVP_SYNAPSE_POLY)
    {
        if (TriangleFlagBit31Set(FeatureA)) WinnerFlag = 0;
        else { IVP_Ledge_Mindist::do_impact(); return; }
    }
    else if (ShapeTypeA == IVP_SYNAPSE_POINT || ShapeTypeA == IVP_SYNAPSE_BALL)
    {
        if (ShapeTypeB < IVP_SYNAPSE_POINT || ShapeTypeB > IVP_SYNAPSE_BALL)
        {
            IVP_CoreTrap();
        }
        else if (ShapeTypeB == IVP_SYNAPSE_POINT || ShapeTypeB == IVP_SYNAPSE_BALL)
        {
            IVP_Ledge_Mindist::do_impact();
            return;
        }
        else if (ShapeTypeB == IVP_SYNAPSE_EDGE)
        {
            if (FeatureB->IsPiercing()) WinnerFlag = 1;
            else { IVP_Ledge_Mindist::do_impact(); return; }
        }
        else // ShapeTypeB == IVP_SYNAPSE_POLY
        {
            if (TriangleFlagBit31Set(FeatureB)) WinnerFlag = 1;
            else { IVP_Ledge_Mindist::do_impact(); return; }
        }
    }
    else // ShapeTypeA == IVP_SYNAPSE_EDGE
    {
        if (ShapeTypeB == IVP_SYNAPSE_POINT || ShapeTypeB == IVP_SYNAPSE_BALL)
        {
            if (TriangleFlagBit31Set(FeatureA)) WinnerFlag = 0;
            else { IVP_Ledge_Mindist::do_impact(); return; }
        }
        else if (ShapeTypeB == IVP_SYNAPSE_EDGE)
        {
            if (!FeatureB->IsPiercing())
            {
                if (FeatureA->IsPiercing()) WinnerFlag = 0;
                else { IVP_Ledge_Mindist::do_impact(); return; }
            }
            else if (!FeatureA->IsPiercing())
            {
                WinnerFlag = 1;
            }
            else
            {
                const float RadiusA = LedgetreeNodeRadius(FeatureA->GetCompactLedge());
                const float RadiusB = LedgetreeNodeRadius(FeatureB->GetCompactLedge());
                WinnerFlag = (RadiusA > RadiusB) ? 0 : 1;
            }
        }
        else
        {
            // Any other ShapeTypeB: the same null-store trap (:063626e7).
            IVP_CoreTrap();
        }
    }

    // LAB_063627bb shared epilogue.
    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    IVP_Environment* const Env = Object0->environment;
    Env->mindist_manager->DetachMindist(this);
    float Range0;
    float Range1;
    Env->range_manager->get_coll_range_intra_objects(Object0, synapse[1].l_obj, &Range0, &Range1);
    IVP_InsertHullMindist(this, (float)((double)(Range0) + Range1));
    StateFlags = (StateFlags & 0xffd3ffffu) | 0x100000u;
    SpawnOrDeferChild((float)((double)(Range0) + Range1));
}

// LAB_06362920, vtable 063a5878 slot 7, RET 4 (argument = the mindist
// manager). Over the spawn cap -> detach and insert_invalid_mindist; else the
// RecalcWinnerFlag and 06362650-style epilogue, with DetachMindist on the
// ARGUMENT rather than the environment's manager.
void IVP_Complex_Mindist::exact_mindist_went_invalid(IVP_Mindist_Manager* Manager)
{
    if (get_spawned_mindist_count() > G_IVP_MindistSettings.max_spawned_mindist_count) // DAT_0658ab3c
    {
        Manager->DetachMindist(this);
        Manager->insert_invalid_mindist(this);
        return;
    }

    WinnerFlag = RecalcWinnerFlag(synapse[0].edge, synapse[1].edge);

    Manager->DetachMindist(this);
    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    float Range0;
    float Range1;
    Object0->environment->range_manager->get_coll_range_intra_objects(Object0, synapse[1].l_obj, &Range0, &Range1);
    IVP_InsertHullMindist(this, (float)((double)(Range0) + Range1));
    StateFlags = (StateFlags & 0xffd3ffffu) | 0x100000u;
    SpawnOrDeferChild((float)((double)(Range0) + Range1));
}

// FUN_06362c70, thiscall, RET 4. With w = WinnerFlag: the losing side's ledge
// is the single ledge, the winning side's the root, and FUN_0634e0b0
// re-synchronises Children with this+0x80 as delegator; the change in count
// goes to change_spawned_mindist_count. Retail does no range check on w (the
// recalc family writes 0 / 1 before calling).
void IVP_Complex_Mindist::SpawnOrDeferChild(float Radius)
{
    IVP_Real_Object* const Object1 = synapse[1].l_obj; // read before the cap test
    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    if (get_spawned_mindist_count() > G_IVP_MindistSettings.max_spawned_mindist_count) return; // DAT_0658ab3c

    const int32_t W = WinnerFlag;
    const IVP_Compact_Ledge* Single[2] = { nullptr, nullptr };
    const IVP_Compact_Ledge* Root[2] = { nullptr, nullptr };
    Single[1 - W] = synapse[1 - W].edge->GetCompactLedge();
    Root[W] = synapse[W].edge->GetCompactLedge();

    const int32_t OldCount = static_cast<int32_t>(Children.n_elems);
    IVP_Create_Exact_Mindists(Object0, Object1, Radius, &Children,
                              Single[0], Single[1], Root[0], Root[1],
                              static_cast<IVP_Collision_Delegator*>(this));
    change_spawned_mindist_count(static_cast<int32_t>(Children.n_elems) - OldCount);
}

// FUN_06362d60: drop back out of complex/recursive tracking, then clear bits
// 12/13 of StateFlags.
void IVP_Complex_Mindist::DeactivateToSimple()
{
    ClearChildren();
    IVP_Mindist_Manager* const Manager = synapse[0].l_obj->environment->mindist_manager;
    Manager->remove_hull_mindist(this);
    Manager->insert_exact_mindist_no_recalc(this);
    StateFlags &= 0xffffcfffu;
}

// FUN_06362d90 -- the library's rec_hull_limit_exceeded_event, reached from
// IVP_Ledge_Mindist::hull_limit_exceeded_event (0634fa40) when
// (StateFlags & 0x3c0000) == 0x100000. A valid mindist whose SeparationGap
// exceeds DAT_0658ab1c (friction_dist; <= or unordered skips) drops back to
// simple tracking; otherwise ++env->range_intra_exceeded (+0x78), respawn the
// children, and re-key both hull pool entries by the intra-object ranges
// (`now` read once; each key unrounded until stored).
void IVP_Complex_Mindist::RecalcAndMaybeDeactivate()
{
    recalc_mindist(); // FUN_06352b90, result unused

    if (((StateFlags & 0xc000u) == 0) && (SeparationGap > G_IVP_MindistSettings.friction_dist))
    {
        DeactivateToSimple();
        return;
    }

    IVP_Real_Object* const Object1 = synapse[1].l_obj;
    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    IVP_Environment* const Env = Object0->environment;
    float Range0;
    float Range1;
    Env->range_manager->get_coll_range_intra_objects(Object0, Object1, &Range0, &Range1);
    ++Env->range_intra_exceeded;

    SpawnOrDeferChild((float)((double)(Range0) + Range1));

    const float NowBase = Env->current_time.time_base;
    const float NowOffset = Env->current_time.time_offset;
    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_Synapse_Real* const Syn = &synapse[I];
        IVP_Hull_Manager& Hull = Syn->l_obj->hull_manager;
        Hull.PoolRemove(static_cast<uint16_t>(Syn->HullPoolSlot));
        const float Key = (float)(
            (((((double)(NowBase) - Hull.LastEditTimeHi) + NowOffset) - Hull.LastEditTimeLo)
             * Hull.OuterGradient + Hull.OuterValueAtLastEdit)
            + ((I == 0) ? Range0 : Range1));
        Syn->HullPoolSlot = Hull.PoolInsert(static_cast<IVP_Listener_Hull*>(Syn), Key);
    }
}

// FUN_06362a30. Delete every child from the top down (null entries skipped,
// elems re-read), report -n (n = the count on entry) through
// change_spawned_mindist_count, then clear the vector (elems is freed unless
// it is the inline this+0x90).
void IVP_Complex_Mindist::ClearChildren()
{
    const int32_t OldCount = static_cast<int32_t>(Children.n_elems);
    for (int32_t I = OldCount - 1; I >= 0; --I)
    {
        IVP_Collision* Child = Children.elems[I];
        if (Child != nullptr)
            delete Child;
    }
    change_spawned_mindist_count(-OldCount);
    Children.Clear();
}

// LAB_06362c00, RET 4: slot 0 of the +0x80 delegator sub-object. The same
// O(1) swap-with-last removal as IVP_OV_Element::remove_oo_collision
// (FUN_0635c250), on Children.
void IVP_Complex_Mindist::collision_is_going_to_be_deleted_event(IVP_Collision* Collision)
{
    // IndexInElement0 is trusted only if in range and pointing back here.
    int32_t Index = Collision->IndexInElement0;
    if (Index < 0 || Index >= static_cast<int32_t>(Children.n_elems) || Children.elems[Index] != Collision)
        Index = Collision->IndexInElement1;

    --Children.n_elems;
    const int32_t Last = static_cast<int32_t>(Children.n_elems);
    if (Last > Index) // signed
    {
        IVP_Collision* Moved = Children.elems[Last];
        Children.elems[Index] = Moved;
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

// FUN_06362ec0, slot 3 of the +0x80 sub-object. Retail asks the owning
// delegator TWICE when the first answer is positive.
int32_t IVP_Complex_Mindist::get_spawned_mindist_count()
{
    if (delegator->get_spawned_mindist_count() > 0)
        return delegator->get_spawned_mindist_count();
    return SpawnedMindistCount; // this+0x90
}

// LAB_06362ea0, RET 4, slot 2 of the +0x80 sub-object.
void IVP_Complex_Mindist::change_spawned_mindist_count(int32_t Change)
{
    SpawnedMindistCount += Change;
    delegator->change_spawned_mindist_count(Change);
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Base::IVP_Mindist_Base(Delegator)
// FUN_0634db70, thiscall, RET 4: IVP_Collision's inlined ctor, the eh vector
// ctor iterator over synapse[2] (FUN_06391ef0; LAB_0634dd00 only stores vtable
// 063a55cc), StateFlags = (x & 0xcfc00000) | 0x0fc00000, vptr 063a55a4,
// +0x5c = 0, +0x04 = 0xffff.
// DEVIATION (determinism): retail's StateFlags store keeps bits 31/30 and
// 29..22 of uninitialised heap memory, and the synapse fields, +0x50..+0x58
// and +0x60..+0x6f are left unwritten until init_mindist / the solver; the
// port zeroes them first.
// ---------------------------------------------------------------------------
IVP_Mindist_Base::IVP_Mindist_Base(IVP_Collision_Delegator* Delegator)
    : IVP_Collision(Delegator)
    , StateFlags(0)
    , CombinedRadius(0.0f)
    , SeparationGap(0.0f)
    , SecondaryNormalDot(0.0f)
    , CombinedHullSpread(0.0f)
    , SeparatingNormal{ 0.0f, 0.0f, 0.0f }
    , _pad6c{}
{
    for (int32_t I = 0; I < 2; ++I) // port-only (LAB_0634dd00 stores the vptr only)
    {
        synapse[I].HullPoolSlot = 0;
        synapse[I].next = synapse[I].prev = nullptr;
        synapse[I].l_obj = nullptr;
        synapse[I].edge = nullptr;
        synapse[I].mindist_offset = 0;
        synapse[I].status = 0;
    }
    StateFlags = (StateFlags & 0xcfc00000u) | 0x0fc00000u;
    CombinedHullSpread = 0.0f;
    EventQueueHandle = 0xffff;
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::IVP_Ledge_Mindist(Environment, Delegator)
// FUN_0634dd10, RET 8: base ctor; +0x70 = +0x7c = 0; vptr 063a55e0;
// ++env->sum_of_mindists (+0x6c); ++env->mindists_generated (+0x70).
// DEVIATION (determinism): ManagerNext / ManagerPrev (+0x74 / +0x78) are not
// written by retail here; the port zeroes them.
// ---------------------------------------------------------------------------
IVP_Ledge_Mindist::IVP_Ledge_Mindist(IVP_Environment* Environment, IVP_Collision_Delegator* Delegator)
    : IVP_Mindist_Base(Delegator)
    , LastDispatchTick(0)
    , ManagerNext(nullptr)
    , ManagerPrev(nullptr)
    , LastPretensionEdge(nullptr)
{
    ++Environment->sum_of_mindists;
    ++Environment->mindists_generated;
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::~IVP_Ledge_Mindist -- FUN_0634dd60 (deleting dtor
// FUN_0634dd40; also run after IVP_Complex_Mindist's dtor 06362b50).
//   Leaves the phantoms if in them, then by the signed 4-bit field at bits
//   18..21: 2 -> remove_invalid_mindist, 3 -> DetachMindist, 4/5 ->
//   remove_hull_mindist, else nothing. Releases both ledges (surface manager
//   slot 8) and notifies the delegator (slot 0). The vptr stores and the eh
//   vector dtor over the synapses (LAB_0634dc50) are the C++ member/base dtors.
// ---------------------------------------------------------------------------
IVP_Ledge_Mindist::~IVP_Ledge_Mindist()
{
    IVP_Environment* const Env = synapse[0].l_obj->environment;
    --Env->sum_of_mindists;
    ++Env->mindists_deleted;
    IVP_Mindist_Manager* const Manager = Env->mindist_manager;

    if ((StateFlags & 0xc00u) != 0)
        IVP_RemoveMindistFromPhantoms(this);

    switch (static_cast<int32_t>(StateFlags << 10) >> 28)
    {
    case 2:
        Manager->remove_invalid_mindist(this);
        break;
    case 3:
        Manager->DetachMindist(this);
        break;
    case 4:
    case 5:
        Manager->remove_hull_mindist(this);
        break;
    default:
        break;
    }

    for (int32_t I = 0; I < 2; ++I)
    {
        synapse[I].l_obj->surface_manager->remove_reference_to_ledge(
            const_cast<IVP_Compact_Ledge*>(synapse[I].get_ledge()));
    }

    delegator->collision_is_going_to_be_deleted_event(this);
}

// FUN_0634ded0. See the declaration.
const IVP_Compact_Ledge* IVP_Synapse_Real::get_ledge() const
{
    const IVP_Compact_Triangle* const Triangle = reinterpret_cast<const IVP_Compact_Triangle*>(
        reinterpret_cast<uintptr_t>(edge) & ~static_cast<uintptr_t>(0x0F));
    return Triangle->GetCompactLedge();
}

// ---------------------------------------------------------------------------
// IVP_Synapse_Real's IVP_Listener_Hull slots (vtable 063a55cc). Each recovers
// the mindist as `this + (int16)[this+0x18]`.
// ---------------------------------------------------------------------------
// LAB_0634f9e0, RET 8: FUN_0634fa40 with the intrusion value; the hull
// manager argument is not read.
void IVP_Synapse_Real::hull_limit_exceeded_event(IVP_Hull_Manager* /*HullManager*/, float HullIntrusionValue)
{
    get_mindist()->hull_limit_exceeded_event(HullIntrusionValue);
}

// LAB_0634fa00, RET 4: the mindist's scalar deleting destructor, null-tested.
void IVP_Synapse_Real::hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* /*HullManager*/)
{
    IVP_Ledge_Mindist* const Mindist = get_mindist();
    if (Mindist != nullptr)
        delete Mindist;
}

// LAB_0634f9c0, RET 8: FUN_0634fa20.
void IVP_Synapse_Real::hull_manager_is_reset(float DeltaTime, float CenterDeltaTime)
{
    get_mindist()->hull_manager_is_reset(DeltaTime, CenterDeltaTime);
}

namespace
{
    // FUN_0628be60 (thiscall, RET 0xc): Out = A + B * Scale, one float
    // rounding per component.
    void IVP_PointAddMultiple(Vector& Out, const Vector& A, const Vector& B, float Scale)
    {
        Out.x = (float)((double)(Scale) * B.x + A.x);
        Out.y = (float)((double)(Scale) * B.y + A.y);
        Out.z = (float)((double)(Scale) * B.z + A.z);
    }
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::hull_limit_exceeded_event -- FUN_0634fa40, RET 4, the
// library's mindist_hull_limit_exceeded_event.
//   A recursive mindist in hull state (0x3c0000 == 0x100000) goes to 06362d90.
//   Otherwise both cores' positions are extrapolated to now (time of Syn0's
//   environment), and unless StateFlags & 0x30000 the gap is advanced by the
//   hull spread and the change in normal projection:
//     NewGap = (Gap - (S - CombinedHullSpread)) - (SecondaryNormalDot - dot)
//              + HullIntrusionValue.
//   If NewGap > delta_time * SumR * 6.0f (DAT_063a5620), the cached values
//   are updated and both hull pool entries re-keyed by NewGap / SumR * r_i;
//   otherwise (<= or unordered) the mindist is re-filed: remove_hull_mindist,
//   then insert_lazy_mindist when (StateFlags & 0x3000) == 0x1000, else
//   insert_exact_mindist.
//   r_i = (MaxSurfaceRotSpeed + CurrentSpeed) + 1e-10f; r0 rounded, r1 rounded but
//   summed extended.
// ---------------------------------------------------------------------------
void IVP_Ledge_Mindist::hull_limit_exceeded_event(float HullIntrusionValue)
{
    if ((StateFlags & 0x3c0000u) == 0x100000u)
    {
        static_cast<IVP_Complex_Mindist*>(this)->RecalcAndMaybeDeactivate();
        return;
    }

    IVP_Synapse_Real* const Syn0 = get_sorted_synapse(0);
    IVP_Synapse_Real* const Syn1 = get_sorted_synapse(1);
    IVP_Real_Object* const Obj0 = Syn0->l_obj;
    IVP_Real_Object* const Obj1 = Syn1->l_obj;
    const IVP_Core* const Core0 = Obj0->core;
    const IVP_Core* const Core1 = Obj1->core;
    IVP_Environment* const Env = Obj0->environment;
    const float NowBase = Env->current_time.time_base;
    const float NowOffset = Env->current_time.time_offset;

    const float Dt0 = (float)((((double)(NowBase) - Core0->TimeOfLastPsi.time_base)
                                          + NowOffset) - Core0->TimeOfLastPsi.time_offset);
    Vector Pos0;
    IVP_PointAddMultiple(Pos0, Core0->PosWorldFCore, Core0->DeltaWorldFCore, Dt0);
    const float Dt1 = (float)((((double)(NowBase) - Core1->TimeOfLastPsi.time_base)
                                          + NowOffset) - Core1->TimeOfLastPsi.time_offset);
    Vector Pos1;
    IVP_PointAddMultiple(Pos1, Core1->PosWorldFCore, Core1->DeltaWorldFCore, Dt1);

    const double Dx = (double)(Pos0.x) - Pos1.x;
    const double Dy = (double)(Pos0.y) - Pos1.y;
    const double Dz = (double)(Pos0.z) - Pos1.z;

    const float R0 = (float)(((double)(Core0->MaxSurfaceRotSpeed) + Core0->CurrentSpeed) + 1.0e-10f);
    const double R1Ext = ((double)(Core1->MaxSurfaceRotSpeed) + Core1->CurrentSpeed) + 1.0e-10f;
    const float R1 = (float)(R1Ext);
    const float SumR = (float)(R1Ext + R0);

    if ((StateFlags & 0x30000u) == 0)
    {
        const double Dot = (Dy * SeparatingNormal.y + Dz * SeparatingNormal.z) + Dx * SeparatingNormal.x;

        const IVP_Hull_Manager& Hull1 = Obj1->hull_manager;
        const IVP_Hull_Manager& Hull0 = Obj0->hull_manager;
        const double Spread1 = (((((double)(NowBase) - Hull1.LastEditTimeHi) + NowOffset) - Hull1.LastEditTimeLo)
                                * ((double)(Hull1.OuterGradient) - Hull1.InnerGradient))
                               + ((double)(Hull1.OuterValueAtLastEdit) - Hull1.InnerValueAtLastEdit);
        const double Spread0 = (((((double)(NowBase) - Hull0.LastEditTimeHi) + NowOffset) - Hull0.LastEditTimeLo)
                                * ((double)(Hull0.OuterGradient) - Hull0.InnerGradient))
                               + ((double)(Hull0.OuterValueAtLastEdit) - Hull0.InnerValueAtLastEdit);
        const double S = Spread1 + Spread0;

        const double A = (double)(SeparationGap) - (S - CombinedHullSpread);
        const double NewGapExt = (A - ((double)(SecondaryNormalDot) - Dot)) + HullIntrusionValue;
        const float NewGap = (float)(NewGapExt);

        const double Threshold = (double)(Env->delta_time) * SumR * 6.0f;
        if (NewGapExt > Threshold)
        {
            CombinedHullSpread = (float)(S);
            SecondaryNormalDot = (float)(Dot);
            SeparationGap = (float)((double)(NewGap) - HullIntrusionValue);
            const float Ratio = (float)((double)(NewGap) / SumR);

            for (int32_t I = 0; I < 2; ++I)
            {
                IVP_Synapse_Real* const Syn = (I == 0) ? Syn0 : Syn1;
                IVP_Hull_Manager& Hull = Syn->l_obj->hull_manager;
                Hull.PoolRemove(static_cast<uint16_t>(Syn->HullPoolSlot));
                const double Base = ((((double)(NowBase) - Hull.LastEditTimeHi) + NowOffset) - Hull.LastEditTimeLo)
                                    * Hull.OuterGradient + Hull.OuterValueAtLastEdit;
                const float Key = (float)((double)(Ratio) * ((I == 0) ? R0 : R1) + Base);
                Syn->HullPoolSlot = Hull.PoolInsert(static_cast<IVP_Listener_Hull*>(Syn), Key);
            }
            return;
        }
    }

    IVP_Mindist_Manager* const Manager = Obj0->environment->mindist_manager;
    Manager->remove_hull_mindist(this);
    if ((StateFlags & 0x3000u) == 0x1000u)
        Manager->insert_lazy_mindist(this);
    else
        Manager->insert_exact_mindist(this);
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::init_mindist -- FUN_0634def0, RET 0x10. FUN_0634e070 /
// FUN_0634e090 are IVP_Synapse_Real::init_synapse_real / set_edge.
// ---------------------------------------------------------------------------
namespace
{
    // CORE trap of the object-type switch (:0634df0f / :0634dfa2).
    void IVP_InitMindistBadObjectType()
    {
        IVP_CoreTrap();
    }
}

void IVP_Ledge_Mindist::init_mindist(IVP_Real_Object* Object0, IVP_Real_Object* Object1,
                                     IVP_Compact_Edge* Edge0, IVP_Compact_Edge* Edge1)
{
    IVP_Synapse_Real* Syn0 = &synapse[0];
    IVP_Synapse_Real* Syn1 = &synapse[1];

    // ---- Object1 -> Syn1 ----
    switch (Object1->object_type)
    {
    case IVP_POLYGON: // polygon: status 0, take a ledge reference
        Syn1->init_synapse_real(this, Object1);
        Syn1->set_edge(Edge1, IVP_SYNAPSE_POINT);
        Object1->surface_manager->add_reference_to_ledge(Edge1->GetCompactLedge()); // slot 7
        break;
    case IVP_BALL: // ball
        // Two balls are ordered by the unsigned dword at +0xa4: Object0 keeps
        // synapse A only when it is a ball too and its value is >= Object1's.
        if (!(Object0->object_type == IVP_BALL &&
              reinterpret_cast<uintptr_t>(Object0->client_data) >= reinterpret_cast<uintptr_t>(Object1->client_data)))
        {
            IVP_Synapse_Real* Tmp = Syn0;
            Syn0 = Syn1;
            Syn1 = Tmp;
        }
        Syn1->init_synapse_real(this, Object1);
        Syn1->set_edge(Edge1, IVP_SYNAPSE_BALL);
        break;
    default:
        IVP_InitMindistBadObjectType(); // then continues with Object0
        break;
    }

    // ---- Object0 -> Syn0 ----
    switch (Object0->object_type)
    {
    case IVP_POLYGON:
        Syn0->init_synapse_real(this, Object0);
        Syn0->set_edge(Edge0, IVP_SYNAPSE_POINT);
        Object0->surface_manager->add_reference_to_ledge(Edge0->GetCompactLedge());
        break;
    case IVP_BALL:
        Syn0->init_synapse_real(this, Object0);
        Syn0->set_edge(Edge0, IVP_SYNAPSE_BALL);
        break;
    default:
        IVP_InitMindistBadObjectType();
        break;
    }

    // Both owner offsets rewritten unconditionally.
    Syn0->mindist_offset = static_cast<int16_t>(reinterpret_cast<char*>(this) - reinterpret_cast<char*>(Syn0));
    Syn1->mindist_offset = static_cast<int16_t>(reinterpret_cast<char*>(this) - reinterpret_cast<char*>(Syn1));

    CombinedRadius = Object0->extra_radius + Object1->extra_radius;

    // Without phantoms the mindist goes straight to the exact list.
    IVP_Mindist_Manager* Manager = Object0->environment->mindist_manager;
    if (Object0->controller_phantom == nullptr && Object1->controller_phantom == nullptr)
    {
        Manager->insert_exact_mindist(this);                  // FUN_0634e9f0
        return;
    }
    StateFlags = (StateFlags & 0xffffdfffu) | 0x1000u;
    Manager->insert_lazy_mindist(this);                       // FUN_0634eb00
}

// ---------------------------------------------------------------------------
// FUN_06355c10 and FUN_063562d0 (declared in ivp_listmanager_topology.h) live
// here next to the IVP_Contact_Point::PopulateOwner / material helpers.
// ---------------------------------------------------------------------------

// FUN_06355c10, thiscall on Entry (AttachedCtx is its first member), RET 4;
// the caller (FUN_06359152, :0635914c) passes its [EDI+0x20] friction system.
// The live count starts at n_elems and each element is re-read per iteration;
// Record->Owner is read AFTER FUN_0635a1e0 (which replaces it). A record whose
// owner's packed +0x54 dword is 0x100 under mask 0x300 is removed
// (FUN_06355490, RemoveDistFromSystem) and not counted.
int IVP_ListManager_CorePairEntry::RefreshAttachedCtxAndCountLive(IVP_ListManager* Manager)
{
    int LiveCount = static_cast<int>(AttachedCtx.n_elems);

    for (int I = LiveCount - 1; I >= 0; --I)
    {
        IVP_Contact_Point* Record = AttachedCtx.elems[I];

        Record->UpdateContactFrame();
        IVP_Impact_Solver_Long_Term* Owner = Record->Owner;
        Record->PopulateOwner(Owner);

        const uint32_t PackedState = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<char*>(Owner) + offsetof(IVP_Impact_Solver_Long_Term, StateProcessed));
        if ((PackedState & 0x300) == 0x100)
        {
            --LiveCount;
            Manager->RemoveDistFromSystem(Record);
        }
    }

    return LiveCount;
}

// ---------------------------------------------------------------------------
// LAB_063568a0 (static friction handle vtable 063a5500 slot 0) is
// IVP_Friction_Sys_Static::core_is_going_to_be_deleted_event
// (ivp_friction_solver.cpp).

// FUN_063562d0. MassFactor (+0x30) = reciprocal two-body reduced mass of the
// two cores at the owner's CoordOS[0/1] lever arms (FUN_063463f0,
// calc_conservative_effective_mass); a kinematic core (Flags & 0xc or 0x300)
// contributes nothing and the other side's effective mass is used alone
// (rounded once). SolverMode (low byte of +0x34) = 1 when either material has
// SecondFrictionXEnabled (+0x08). In the two-sided case both masses are
// rounded to float before the combination.
void IVP_Contact_Point::ComputeMassFactor()
{
    IVP_Material* Materials[2];
    ResolveSideMaterials(Materials);

    if (Materials[0]->SecondFrictionXEnabled != 0 || Materials[1]->SecondFrictionXEnabled != 0)
    {
        SolverMode = 1;
    }

    IVP_Core* Core1 = Object1->core;
    IVP_Core* Core2 = Object2->core;

    if ((Core1->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0 || (Core1->Flags & IVP_CORE_PINNED) != 0)
    {
        const double EffMass = Core2->calc_conservative_effective_mass(&Owner->CoordOS[1]);
        MassFactor = (float)(1.0 / EffMass);
        return;
    }
    if ((Core2->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0 || (Core2->Flags & IVP_CORE_PINNED) != 0)
    {
        const double EffMass = Core1->calc_conservative_effective_mass(&Owner->CoordOS[0]);
        MassFactor = (float)(1.0 / EffMass);
        return;
    }

    const float EffMass0 = (float)(Core1->calc_conservative_effective_mass(&Owner->CoordOS[0]));
    const float EffMass1 = (float)(Core2->calc_conservative_effective_mass(&Owner->CoordOS[1]));
    const double Reduced = ((double)(EffMass1) * EffMass0) / ((double)(EffMass1) + EffMass0);
    MassFactor = (float)(1.0 / Reduced);
}


// ---------------------------------------------------------------------------
// IVP_Compact_Triangle_Init (port name): zeroes a 4-float structure. Retail
// first calls FUN_0635aa10 (`AND [field],0x80000000`) on +0x4/+0x8/+0xc and
// then stores 0 to all four, so those calls have no effect and are omitted.
// Single caller 0x0637a4f0; the owning type (possibly a 4-float Hesse plane)
// is not identified.
extern "C" void* IVP_Compact_Triangle_Init(void* Memory)
{
    float* fields = static_cast<float*>(Memory);
    fields[0] = 0.0f;
    fields[1] = 0.0f;
    fields[2] = 0.0f;
    fields[3] = 0.0f;
    return Memory;
}
