// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_impact.h"
#include "ivp_environment.h"
#include "ivp_core.h"
#include "ivp_real_object.h"
#include "ivp_contact_point.h"                 // IVP_Ledge_Mindist::try_to_generate_managed_friction, the contact point helpers
#include "ivp_cluster_manager.h"               // fire_event_pre_collision (FUN_0634b030)
#include "ivp_listener.h"                      // IVP_Event_Collision
#include "ivp_mindist_settings.h"
#include "ivp_controller.h"                    // IVP_Event_Sim
#include "ivp_calc_next_psi_solver.h"
#include "../ivp_collision/ivp_listmanager_topology.h" // FUN_0635a1e0 / FUN_06358c50 / FUN_06355c10
#include "x87_trig.h"
#include <cmath>
#include <cstdint>
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte

// The impact system. A value kept on the x87 stack is double here; a float
// store rounds.

namespace
{
    const float GIvpEpsilon = 0x1.b7cdfep-34f;  // DAT_063a53d8 (0x2edbe6ff, 1e-10f)
    const float GIvpOne = 1.0f;                 // DAT_063a53d0 (0x3f800000)
    const float GIvpHalf = 0.5f;                // DAT_063a5460 (0x3f000000)
    const float GIvpOneTwentyFourth = 0x1.555556p-5f; // DAT_063a5660 (0x3d2aaaab)

    // A core's kinetic energy as FUN_06358680 computes it inline four times
    // (two instruction orders, same expression), left on the x87 stack.
    double IVP_Impact_Core_Energy(const IVP_Core* Core)
    {
        const double RotIx = (double)(Core->RotSpeed.x) * Core->RotInertia.x;
        const double RotIy = (double)(Core->RotSpeed.y) * Core->RotInertia.y;
        const double RotIz = (double)(Core->RotSpeed.z) * Core->RotInertia.z;
        const double SpeedSq = ((double)(Core->Speed.z) * Core->Speed.z +
                                (double)(Core->Speed.y) * Core->Speed.y) +
                               (double)(Core->Speed.x) * Core->Speed.x;
        const double Linear = (SpeedSq * Core->Mass) * GIvpHalf;
        const double Rotational = ((RotIz * Core->RotSpeed.z + RotIy * Core->RotSpeed.y) +
                                   RotIx * Core->RotSpeed.x) * GIvpHalf;
        return Linear + Rotational;
    }
}

// ---------------------------------------------------------------------------
// FUN_06358c00. cos(atan(t)) by its Taylor terms; c and t stay unrounded until
// the two float stores. FPATAN via X87Atan.
// ---------------------------------------------------------------------------
void IVP_Impact_Solver_Long_Term_Get_Cos_Sin_For_Impact(float FrictionVal, float OrthComp,
                                                         float* OutCos, float* OutSin)
{
    const double TanAngle = (std::sqrt((double)(OrthComp)) + GIvpOne) * FrictionVal;
    const double Angle = X87Atan(TanAngle);
    const double Angle2 = Angle * Angle;
    const double Cos = (GIvpOne - Angle2 * GIvpHalf) + (Angle2 * Angle2) * GIvpOneTwentyFourth;
    *OutCos = (float)(Cos);
    *OutSin = (float)(Cos * TanAngle);
}

// ---------------------------------------------------------------------------
// FUN_06358d60. The 0x110-byte IVP_Impact_Solver lives on the stack; the
// vector-constructor call on its +0x84 runs a no-op ctor and is not ported.
// With no second contact core the sides are swapped and the normal negated.
// Friction only when neither core has an AxisLock. No null test anywhere: a
// null contact core 0 in the first branch is dereferenced, as in retail.
// ---------------------------------------------------------------------------
void IVP_Impact_Solver_Long_Term::DoImpactLongTerm(IVP_Core* PushedCores[2], float RescueSpeedVal,
                                                   IVP_Contact_Point* Cp)
{
    IVP_Impact_Solver ImpactSolver;
    Vector NegatedNormal;

    if (ContactCores[1] == nullptr)
    {
        NegatedNormal.x = -ContactNormalWS.x;
        NegatedNormal.y = -ContactNormalWS.y;
        NegatedNormal.z = -ContactNormalWS.z;
        ImpactSolver.Cores[0] = Objects[1]->core;
        ImpactSolver.Cores[1] = ContactCores[0];
        ImpactSolver.ContactCoordsOS[0] = &CoordOS[1];
        ImpactSolver.ContactCoordsOS[1] = &CoordOS[0];
        ImpactSolver.NormalVectorWS = &NegatedNormal;
    }
    else
    {
        IVP_Core* Core0 = ContactCores[0];
        if (Core0 == nullptr)
        {
            Core0 = Objects[0]->core;
        }
        ImpactSolver.Cores[0] = Core0;
        ImpactSolver.ContactCoordsOS[0] = &CoordOS[0];
        ImpactSolver.Cores[1] = ContactCores[1];
        ImpactSolver.ContactCoordsOS[1] = &CoordOS[1];
        ImpactSolver.NormalVectorWS = &ContactNormalWS;
    }

    const float Elasticity = ContactElasticity;
    ImpactSolver.Restitution = Elasticity;
    // +0x10 of the long-term data is Ipion's contact-situation speed.
    ImpactSolver.FrictionVectorWS = &Speed;
    ImpactSolver.UseFrictionConeBlend = 0;

    if (ImpactSolver.Cores[0]->AxisLock == nullptr && ImpactSolver.Cores[1]->AxisLock == nullptr)
    {
        IVP_Impact_Solver_Long_Term_Get_Cos_Sin_For_Impact(Cp->FrictionCoefficient, Elasticity,
                                                            &ImpactSolver.NormalCoeff, &ImpactSolver.TangentCoeff);
        if (Cp->SolverMode != 0)
        {
            ImpactSolver.CalcSecondFriction(Cp); // FUN_06359380
        }
    }
    else
    {
        IVP_Impact_Solver_Long_Term_Get_Cos_Sin_For_Impact(0.0f, Elasticity,
                                                            &ImpactSolver.NormalCoeff, &ImpactSolver.TangentCoeff);
    }

    ImpactSolver.DoImpactSystem(PushedCores, 1, static_cast<int32_t>(Status), RescueSpeedVal); // FUN_063578a0
}

// ---------------------------------------------------------------------------
// FUN_06358680. The sim unit not to destroy is ObjB's when ObjA's low flags
// byte (signed) is < 8, else ObjA's. Energy deltas: +0x48 = float(E0' - float
// E0) with E0' unrounded, +0x4c = float(float E1' - float E1). The contact
// speed is saved (negated when CoreB is fixed) around the impact system and
// written back before the collision listeners and, for objects with flag
// 0x2000, the cluster manager's pre-collision event fire.
// ---------------------------------------------------------------------------
void IVP_Mindist_Do_Impact(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* ObjA, IVP_Real_Object* ObjB)
{
    IVP_Core* const CoreB = ObjB->core;
    IVP_Core* const CoreA = ObjA->core;
    IVP_Simulation_Unit* const SimUnitNotDestroy =
        (IVP_SignedLowByte(ObjA->flags) < 8) ? CoreB->OwningSimUnit : CoreA->OwningSimUnit;

    IVP_ListManager* Fs;
    int32_t HavingNewDist;
    IVP_Contact_Point* const Cp =
        Mindist->try_to_generate_managed_friction(&Fs, &HavingNewDist, SimUnitNotDestroy, 1); // FUN_06356bc0
    IVP_Impact_Solver_Long_Term* const LongTerm = Cp->Owner;

    IVP_ListManager_CorePairEntry* const Pair = Fs->FindCorePairEntryExact(CoreA, CoreB); // FUN_06356280

    IVP_Environment* const Env = Mindist->synapse[0].l_obj->environment;
    // +0x1c/+0x20 of the core pair is an IVP_Time (Ipion last_impact_time).
    IVP_Time* const LastImpactTime = &Pair->LastImpactTime;
    const IVP_Time OldTime = *LastImpactTime;
    const IVP_Time NowTime = Env->current_time;
    *LastImpactTime = NowTime;

    const float RescueSpeed =
        (float)(Cp->GetRescueSpeedImpact(ObjA->environment)); // FUN_06359560

    IVP_Core* const Core0 = LongTerm->Objects[0]->core;
    IVP_Core* const Core1 = LongTerm->Objects[1]->core;
    const float EnergyBefore0 = (float)(IVP_Impact_Core_Energy(Core0));
    const float EnergyBefore1 = (float)(IVP_Impact_Core_Energy(Core1));

    IVP_Core* PushedCores[2];
    LongTerm->DoImpactLongTerm(PushedCores, RescueSpeed, Cp); // FUN_06358d60

    Vector SavedSpeed = LongTerm->Speed;
    if ((CoreB->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
    {
        SavedSpeed.x = -SavedSpeed.x;
        SavedSpeed.y = -SavedSpeed.y;
        SavedSpeed.z = -SavedSpeed.z;
    }

    {
        IVP_Impact_System ImpactSystem;
        ImpactSystem.init_and_solve_impact_system(Mindist, Fs, Pair, Cp); // FUN_06358f40
    }

    const double EnergyAfter0 = IVP_Impact_Core_Energy(Core0);
    const float EnergyAfter1 = (float)(IVP_Impact_Core_Energy(Core1));
    LongTerm->EnergyDelta0 = (float)(EnergyAfter0 - EnergyBefore0);
    LongTerm->EnergyDelta1 = (float)((double)(EnergyAfter1) - EnergyBefore1);

    IVP_Event_Collision Event;
    Event.d_time_since_last_collision = (float)(
        (((double)(NowTime.time_base) - OldTime.time_base) + NowTime.time_offset) - OldTime.time_offset);
    Event.environment = Env;
    Event.contact_situation = reinterpret_cast<IVP_Contact_Situation*>(LongTerm);
    LongTerm->Speed = SavedSpeed;

    Env->fire_event_pre_collision(&Event); // FUN_0634cf60
    if ((ObjA->flags & 0x2000u) != 0)
    {
        Env->cluster_manager->fire_event_pre_collision(ObjA, &Event); // FUN_0634b030
    }
    if ((ObjB->flags & 0x2000u) != 0)
    {
        Env->cluster_manager->fire_event_pre_collision(ObjB, &Event);
    }
}

// ---------------------------------------------------------------------------
// FUN_06358f40. Loops test_loop_all_pairs; past 5000 iterations the mindist
// is deleted (slot 4, when non-null) and the loop left.
// ---------------------------------------------------------------------------
void IVP_Impact_System::init_and_solve_impact_system(IVP_Ledge_Mindist* Mindist, IVP_ListManager* Fs,
                                                     IVP_ListManager_CorePairEntry* Pair,
                                                     IVP_Contact_Point* Cp)
{
    IVP_Core* const Core0 = Cp->Object1->core;
    IVP_Core* const Core1 = Cp->Object2->core;
    ImpactsWhileSystem = 0;
    AssociatedFs = Fs;
    LEnvironment = Pair->CoreA->Environment;

    if (Core0 != nullptr && (Core0->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0 && (Core0->Flags & IVP_CORE_PINNED) == 0)
    {
        add_pushed_core_with_pairs_except(Core0, Pair); // FUN_063590e0
    }
    IVP_Core* const PairCoreA = Pair->CoreA;
    if ((PairCoreA->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0 && (PairCoreA->Flags & IVP_CORE_PINNED) == 0)
    {
        SyncedCores.Add(PairCoreA);
    }
    if (Core1 != nullptr && (Core1->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0 && (Core1->Flags & IVP_CORE_PINNED) == 0)
    {
        add_pushed_core_with_pairs_except(Core1, Pair);
    }
    IVP_Core* const PairCoreB = Pair->CoreB;
    if ((PairCoreB->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0 && (PairCoreB->Flags & IVP_CORE_PINNED) == 0)
    {
        SyncedCores.Add(PairCoreB);
    }

    Pairs.Add(Pair);                     // FUN_06359090
    recalc_pair_dists_except(Pair, Cp);  // FUN_06358ee0

    int32_t Count = 0;
    if (test_loop_all_pairs() == 1) // FUN_063591a0
    {
        for (;;)
        {
            ++Count;
            ++ImpactsWhileSystem;
            if (ImpactsWhileSystem > 5000)
            {
                if (Mindist != nullptr)
                {
                    delete Mindist; // slot 4, deleting destructor
                }
                break;
            }
            if (test_loop_all_pairs() != 1)
            {
                break;
            }
        }
    }

    LEnvironment->impact_loop_count += Count + 1;
    recalc_all_affected_cores(); // FUN_063596b0
}

// ---------------------------------------------------------------------------
// FUN_06358ee0. n_elems read once, elems every pass; Cp->Owner is read after
// FUN_0635a1e0. Dists whose owner's +0x54 dword has (& 0x300) == 0x100 are
// removed from the friction system.
// ---------------------------------------------------------------------------
void IVP_Impact_System::recalc_pair_dists_except(IVP_ListManager_CorePairEntry* Pair,
                                                 IVP_Contact_Point* CpNot)
{
    for (int32_t Index = static_cast<int32_t>(Pair->AttachedCtx.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Contact_Point* const Cp = Pair->AttachedCtx.elems[Index];
        if (Cp == CpNot)
        {
            continue;
        }
        Cp->UpdateContactFrame();            // FUN_0635a1e0
        IVP_Impact_Solver_Long_Term* const LongTerm = Cp->Owner;
        Cp->PopulateOwner(LongTerm);   // FUN_06358c50
        if ((LongTerm->FrictionIsBroken & 0x3u) == 0x1u)
        {
            AssociatedFs->RemoveDistFromSystem(Cp);      // FUN_06355490
        }
    }
}

// ---------------------------------------------------------------------------
// FUN_063590c0. `this` is not used. Synchronises when the signed low byte of
// MovementState (+0x60) is below 8.
// ---------------------------------------------------------------------------
void IVP_Impact_System::synchronize_core_if_simulated(IVP_Core* Core)
{
    if (IVP_SignedLowByte(Core->MovementState) < 8)
    {
        Core->recalc_interpolated_frame(); // FUN_06347240 (Ipion synchronize_with_rot_z)
    }
}

// ---------------------------------------------------------------------------
// FUN_063590e0. n_elems read once, elems re-read every pass.
// ---------------------------------------------------------------------------
void IVP_Impact_System::add_pushed_core_with_pairs_except(IVP_Core* Core, IVP_ListManager_CorePairEntry* PairNot)
{
    PushedCores.Add(Core);
    static_cast<IVP_Old_Sync_Rot_Z*>(Core->OldSyncInfo)->WasPushedDuringImpactSystem = 1;

    for (int32_t Index = static_cast<int32_t>(AssociatedFs->CorePairEntries.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_ListManager_CorePairEntry* const Pair = AssociatedFs->CorePairEntries.elems[Index];
        if (Pair->CoreA != Core && Pair->CoreB != Core)
        {
            continue;
        }
        if (Pair == PairNot)
        {
            continue;
        }
        if (Pairs.Contains(Pair)) // FUN_06359170
        {
            continue;
        }
        if (Pair->RefreshAttachedCtxAndCountLive(AssociatedFs) > 0) // FUN_06355c10
        {
            Pairs.Add(Pair); // FUN_06359090
        }
    }
}

// ---------------------------------------------------------------------------
// FUN_063591a0. Picks the dist with the lowest priority (below
// max_coll_dist, DAT_0658ab18; NaN also wins) over all pairs whose two cores'
// packed state bits do not intersect (the bit packing is retail's, kept as
// is), impacts it and extends the system by the pushed cores. Returns 0 when
// no dist qualifies.
// ---------------------------------------------------------------------------
int32_t IVP_Impact_System::test_loop_all_pairs()
{
    IVP_Contact_Point* BestCp = nullptr;
    IVP_ListManager_CorePairEntry* BestPair = nullptr;
    float BestPriority = G_IVP_MindistSettings.max_coll_dist;

    for (int32_t PairIndex = static_cast<int32_t>(Pairs.n_elems) - 1; PairIndex >= 0; --PairIndex)
    {
        IVP_ListManager_CorePairEntry* const Pair = Pairs.elems[PairIndex];
        const IVP_Core* const CoreB = Pair->CoreB;
        const IVP_Core* const CoreA = Pair->CoreA;
        const int32_t StateB = (static_cast<int32_t>((CoreB->Flags & 0xfffffffcu) << 28) >> 6) |
                               static_cast<int32_t>((CoreB->MovementState & 0xffffff00u) << 16);
        const int32_t StateA = (static_cast<int32_t>(CoreA->Flags << 28) >> 6) |
                               static_cast<int32_t>(CoreA->MovementState << 16);
        if ((StateA & StateB) != 0)
        {
            continue;
        }

        for (int32_t CpIndex = static_cast<int32_t>(Pair->AttachedCtx.n_elems) - 1; CpIndex >= 0; --CpIndex)
        {
            IVP_Contact_Point* const Cp = Pair->AttachedCtx.elems[CpIndex];
            if (Cp->Owner->StateProcessed != 1)
            {
                ++LEnvironment->impact_priority_calc_count;
                Cp->CalcImpactPriority(); // FUN_063595c0
            }
            const float Priority = Cp->Owner->WinnerPriority;
            if (!(Priority >= BestPriority))
            {
                BestPriority = Priority;
                BestCp = Cp;
                BestPair = Pair;
            }
        }
    }

    if (BestCp == nullptr)
    {
        return 0;
    }

    for (int32_t Side = 1; Side >= 0; --Side)
    {
        IVP_Core* const Core = BestCp->Owner->ContactCores[Side];
        if (Core == nullptr || Core->OldSyncInfo != nullptr)
        {
            continue;
        }
        SyncedCores.Add(Core);
        synchronize_core_if_simulated(Core); // FUN_063590c0
    }

    IVP_Impact_Solver_Long_Term* const LongTerm = BestCp->Owner;
    ++LongTerm->Status;
    IVP_Core* PushedCoresOfImpact[2];
    BestCp->Owner->DoImpactLongTerm(PushedCoresOfImpact,
                                    BestCp->Owner->LastImpactTimeStep, BestCp); // FUN_06358d60

    for (int32_t Side = 1; Side >= 0; --Side)
    {
        IVP_Core* const Core = PushedCoresOfImpact[Side];
        if (Core == nullptr || (Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0 || (Core->Flags & IVP_CORE_PINNED) != 0)
        {
            continue;
        }
        if (static_cast<IVP_Old_Sync_Rot_Z*>(Core->OldSyncInfo)->WasPushedDuringImpactSystem == 0)
        {
            add_pushed_core_with_pairs_except(Core, BestPair); // FUN_063590e0
        }
        else
        {
            clear_processed_state_of_core_dists(Core); // FUN_06359340
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// FUN_06359340. No null test on the hash entry (FUN_06347c40).
// ---------------------------------------------------------------------------
void IVP_Impact_System::clear_processed_state_of_core_dists(IVP_Core* Core)
{
    IVP_Friction_Info_For_Core* const Info = static_cast<IVP_Friction_Info_For_Core*>(
        Core->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(AssociatedFs))));
    for (int32_t Index = static_cast<int32_t>(Info->FrDists.n_elems) - 1; Index >= 0; --Index)
    {
        Info->FrDists.elems[Index]->Owner->StateProcessed = 0;
    }
}

// ---------------------------------------------------------------------------
// FUN_063596b0. The event sim's +0xc is never written. Delta > 1e-10 is an
// ordered compare (NaN takes the environment's step).
// ---------------------------------------------------------------------------
void IVP_Impact_System::recalc_all_affected_cores()
{
    for (int32_t Index = static_cast<int32_t>(SyncedCores.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Core* const Core = SyncedCores.elems[Index];
        if (static_cast<IVP_Old_Sync_Rot_Z*>(Core->OldSyncInfo)->WasPushedDuringImpactSystem == 0)
        {
            Core->restore_fast_sim_snapshot(); // FUN_063473b0
        }
        Core->OldSyncInfo = nullptr;
    }

    IVP_Environment* const Env = LEnvironment;
    const float Delta = (float)(
        (((double)(Env->next_psi_time_base) - Env->current_time.time_base) + Env->next_psi_time_offset) -
        Env->current_time.time_offset);

    {
        IVP_U_Vector_Fixed<IVP_Hull_Manager, 0x100> ActiveHullManagers;
        for (int32_t Index = static_cast<int32_t>(PushedCores.n_elems) - 1; Index >= 0; --Index)
        {
            IVP_Core* const Core = PushedCores.elems[Index];
            if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
            {
                continue;
            }
            IVP_Event_Sim EventSim;
            EventSim.environment = LEnvironment;
            if (Delta > GIvpEpsilon)
            {
                EventSim.delta_time = Delta;
                EventSim.i_delta_time = (float)((double)(GIvpOne) / Delta);
            }
            else
            {
                EventSim.delta_time = LEnvironment->delta_time;
                EventSim.i_delta_time = LEnvironment->inv_delta_time;
            }
            IVP_Calc_Next_PSI_Solver Solver(Core);
            Solver.calc_next_PSI_matrix(&EventSim, &ActiveHullManagers); // FUN_06351310
            Core->OldSyncInfo = nullptr;
            clear_processed_state_of_core_dists(Core); // FUN_06359340
        }
        IVP_Calc_Next_PSI_Solver::commit_all_hull_managers(LEnvironment, &ActiveHullManagers); // FUN_06351f80
    }

    for (int32_t Index = static_cast<int32_t>(PushedCores.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Core* const Core = PushedCores.elems[Index];
        if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
        {
            Core->notify_objects_and_cache_env_field(); // FUN_06347be0
        }
    }
}
